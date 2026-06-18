/*
 * container.c — process isolation via Linux namespaces
 *
 * Core idea: instead of fork(), we use clone() with namespace flags.
 * This gives each container its own isolated view of PIDs, hostname,
 * and mounts — the same primitives Docker is built on.
 */
#include <sched.h>  /* clone(), CLONE_NEW*               */
#include <signal.h> /* SIGKILL, SIGCHLD                  */
#include <stdio.h>
#include <stdlib.h> /* malloc(), free()                  */
#include <string.h> /* memset(), strncpy(), strerror()   */
#include <sys/types.h>
#include <sys/wait.h> /* waitpid(), WNOHANG                */
#include <unistd.h>   /* execvp(), sethostname()           */
#include <errno.h>
#include "hk_container.h"
#include "hk_log.h"

/* ── container table ─────────────────────────────────────────
 *
 * A flat array of descriptors. Simple and cache-friendly.
 * Slots with state == HK_STATE_EMPTY are free to reuse.
 *
 * Static = private to this file. Nobody outside can touch
 * this array directly — they go through our API functions.
 * ─────────────────────────────────────────────────────────── */
static hk_container_t _table[HK_MAX_CONTAINERS];
static int _next_id = 1; /* auto-incrementing id  */

/* ── clone() trampoline ──────────────────────────────────────
 *
 * clone() needs a function pointer to call in the child.
 * This is that function. It runs inside the new namespaces.
 *
 * By the time this executes, the kernel has already set up
 * the new PID/UTS/mount namespaces for us.
 * ─────────────────────────────────────────────────────────── */

/*
 * We need to pass two things into the child: the command to exec
 * and the container name (for sethostname). Bundle them in a struct.
 */
typedef struct
{
    char *const *cmd; /* argv[] for execvp — NULL terminated */
    const char *name; /* becomes the container's hostname    */
    const char *rootfs;
} _child_args_t;

static int _container_entry(void *arg)
{
    _child_args_t *a = (_child_args_t *)arg;

    /* Give the UTS namespace a hostname matching the container name. */
    if (sethostname(a->name, strlen(a->name)) < 0)
        perror("sethostname");

    /*
     * ── VFS isolation via chroot() ──
     *
     * chroot() changes what THIS process (and its descendants)
     * consider to be "/". After this call, any path the process
     * opens is resolved relative to a->rootfs instead of the
     * real filesystem root.
     *
     * Note: a determined process with enough privileges can
     * technically escape a chroot jail (e.g. via fchdir tricks
     * with an open fd to the real root). Real container runtimes
     * use pivot_root() + dropping capabilities to close this gap.
     * We use chroot() here because the concept is identical and
     * far simpler to learn from.
     */
    if (a->rootfs != NULL)
    {
        if (chroot(a->rootfs) < 0)
        {
            perror("chroot");
            return 126;
        }

        /*
         * CRITICAL: chroot() does NOT change the current working
         * directory. Without this chdir, relative paths inside
         * the container would still resolve against the OLD cwd,
         * which no longer makes sense from inside the jail.
         */
        if (chdir("/") < 0)
        {
            perror("chdir");
            return 126;
        }
    }

    /* exec the requested command. */
    execvp(a->cmd[0], a->cmd);

    /* If we reach here execvp failed. */
    perror("execvp");
    return 127;
}

/* ── public API ──────────────────────────────────────────────── */

void hk_container_init(void)
{
    memset(_table, 0, sizeof(_table));
    HK_INFO("container table ready (capacity: %d)", HK_MAX_CONTAINERS);
}

int hk_container_run(const char *name, char *const cmd[], int priority, const char *rootfs)
{
    /* ── 1. find a free slot ── */
    hk_container_t *slot = NULL;
    for (int i = 0; i < HK_MAX_CONTAINERS; i++)
    {
        if (_table[i].state == HK_STATE_EMPTY)
        {
            slot = &_table[i];
            break;
        }
    }
    if (!slot)
    {
        HK_ERR("container table is full (%d/%d)", HK_MAX_CONTAINERS, HK_MAX_CONTAINERS);
        return HK_FULL;
    }

    /* ── 2. allocate a stack for the cloned child ──
     *
     * clone() requires us to manage the child's stack manually.
     * We malloc 1MB and pass a pointer to the TOP because x86
     * stacks grow from high address → low address.
     */
    char *stack = malloc(HK_STACK_SIZE);
    if (!stack)
    {
        HK_ERR("failed to allocate stack: %s", strerror(errno));
        return HK_ERR;
    }
    char *stack_top = stack + HK_STACK_SIZE;

    /* ── 3. set up child args ── */
    _child_args_t cargs = {
        .cmd = cmd,
        .name = name,
        .rootfs = rootfs};

    /*
     * ── 4. clone() — the key syscall ──
     *
     * Flags breakdown:
     *   SIGCHLD      → send SIGCHLD to parent when child exits
     *                  (required for waitpid() to work)
     *   CLONE_NEWPID → child gets its own PID namespace
     *                  (it sees itself as PID 1)
     *   CLONE_NEWUTS → child gets its own hostname/domainname
     *   CLONE_NEWNS  → child gets its own mount namespace
     *
     * Arguments: (function, stack_top, flags, arg)
     */
    int clone_flags = SIGCHLD | CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS;

    pid_t pid = clone(_container_entry, stack_top, clone_flags, &cargs);

    if (pid < 0)
    {
        HK_ERR("clone() failed: %s", strerror(errno));
        free(stack);
        return HK_ERR;
    }

    /* ── 5. fill the descriptor ── */
    slot->id = _next_id++;
    slot->pid = pid;
    slot->priority = (priority < 0) ? 0 : (priority > 9) ? 9
                                                         : priority;
    slot->state = HK_STATE_RUNNING;
    slot->mem_bytes = 0;
    slot->cpu_ticks = 0;
    strncpy(slot->name, name, HK_NAME_MAX - 1);
    if (rootfs != NULL)
        strncpy(slot->rootfs, rootfs, sizeof(slot->rootfs) - 1);
    else
        strncpy(slot->rootfs, "/", sizeof(slot->rootfs) - 1);

    HK_OK("container [%d] '%s' started  pid=%-6d  priority=%d",
          slot->id, slot->name, slot->pid, slot->priority);

    return slot->id;
}

hk_rc_t hk_container_kill(int id)
{
    hk_container_t *c = hk_container_get(id);
    if (!c)
    {
        HK_ERR("no container with id %d", id);
        return HK_NOTFOUND;
    }
    if (c->state != HK_STATE_RUNNING)
    {
        HK_WARN("container [%d] is not running", id);
        return HK_ERR;
    }

    if (kill(c->pid, SIGKILL) < 0)
    {
        HK_ERR("kill(%d) failed: %s", c->pid, strerror(errno));
        return HK_ERR;
    }

    c->state = HK_STATE_STOPPED;
    HK_OK("container [%d] '%s' killed", c->id, c->name);
    return HK_OK;
}

void hk_container_ps(void)
{
    /* Map state enum → display string */
    static const char *state_str[] = {
        [HK_STATE_EMPTY] = "empty",
        [HK_STATE_CREATED] = "created",
        [HK_STATE_RUNNING] = "running",
        [HK_STATE_STOPPED] = "stopped",
    };

    printf("\n");
    printf("  \033[1m%-4s  %-20s  %-8s  %-4s  %s\033[0m\n",
           "ID", "NAME", "PID", "PRI", "STATE");
    printf("  %-4s  %-20s  %-8s  %-4s  %s\n",
           "────", "────────────────────", "────────", "────", "───────");

    int shown = 0;
    for (int i = 0; i < HK_MAX_CONTAINERS; i++)
    {
        hk_container_t *c = &_table[i];
        if (c->state == HK_STATE_EMPTY)
            continue;

        /* Green for running, yellow for stopped */
        const char *col = (c->state == HK_STATE_RUNNING)
                              ? "\033[32m"
                              : "\033[33m";

        printf("  %-4d  %-20s  %-8d  %-4d  %s%s\033[0m\n",
               c->id, c->name, c->pid,
               c->priority, col, state_str[c->state]);
        shown++;
    }

    if (shown == 0)
        printf("  \033[2m(no containers)\033[0m\n");

    printf("\n");
}

hk_container_t *hk_container_get(int id)
{
    for (int i = 0; i < HK_MAX_CONTAINERS; i++)
    {
        if (_table[i].state != HK_STATE_EMPTY && _table[i].id == id)
            return &_table[i];
    }
    return NULL;
}

void hk_container_reap(void)
{
    int status;
    pid_t pid;

    /*
     * waitpid(-1, ..., WNOHANG):
     *   -1     = wait for ANY child
     *   WNOHANG = return immediately if no child has exited
     *             (non-blocking — we don't want to stall the CLI)
     *
     * Returns 0 if no children have exited yet.
     * Returns >0 (the pid) when a child is reaped.
     * Loop until nothing left to reap.
     */
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        for (int i = 0; i < HK_MAX_CONTAINERS; i++)
        {
            if (_table[i].pid == pid &&
                _table[i].state == HK_STATE_RUNNING)
            {
                _table[i].state = HK_STATE_STOPPED;
                HK_INFO("container [%d] '%s' exited (code %d)",
                        _table[i].id,
                        _table[i].name,
                        WEXITSTATUS(status));
                break;
            }
        }
    }
}

int hk_container_count(void)
{
    int n = 0;
    for (int i = 0; i < HK_MAX_CONTAINERS; i++)
        if (_table[i].state == HK_STATE_RUNNING)
            n++;
    return n;
}