/*
 * daemon.c — the hollowkernel background daemon
 *
 * Two threads:
 *   1. accept() loop  — serves CLI client commands
 *   2. scheduler loop — fires hk_scheduler_tick() every HK_TICK_MS
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h> /* pthread_create(), pthread_t          */

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>

#include "hk_daemon.h"
#include "hk_container.h"
#include "hk_scheduler.h"
#include "hk_socket.h"
#include "hk_log.h"

/* ── scheduler thread ────────────────────────────────────────
 *
 * Runs in background, fires a tick every HK_TICK_MS milliseconds.
 * Completely independent of the accept() loop.
 * ─────────────────────────────────────────────────────────── */
static void *_scheduler_thread(void *arg)
{
    (void)arg;

    while (1)
    {
        /*
         * usleep takes microseconds.
         * HK_TICK_MS is milliseconds → multiply by 1000.
         */
        usleep(HK_TICK_MS * 1000);
        hk_scheduler_tick();
    }

    return NULL;
}

/* ── socket setup ────────────────────────────────────────────── */

static int _setup_socket(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        HK_ERR("socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, HK_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    unlink(HK_SOCKET_PATH);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        HK_ERR("bind() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    if (listen(fd, 5) < 0)
    {
        HK_ERR("listen() failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/* ── client handler ──────────────────────────────────────────── */

static void _handle_client(int client_fd)
{
    hk_request_t req;
    hk_response_t resp;

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    if (hk_recv(client_fd, &req, sizeof(req)) < 0)
    {
        HK_ERR("failed to read request");
        return;
    }

    switch (req.type)
    {

    case HK_CMD_RUN:
    {
        char *argv[HK_MAX_ARGS + 1];
        for (int i = 0; i < req.argc && i < HK_MAX_ARGS; i++)
            argv[i] = req.argv[i];
        argv[req.argc] = NULL;

        /* Empty rootfs string means "no isolation" — pass NULL */
        const char *rootfs = (req.rootfs[0] != '\0') ? req.rootfs : NULL;
        int id = hk_container_run(req.name, argv, req.priority, rootfs);
        if (id > 0)
        {
            /*
             * Tell the scheduler about the new container.
             * It will add it to the run queue immediately.
             */
            hk_scheduler_add(id);
            resp.status = 0;
            snprintf(resp.message, sizeof(resp.message),
                     "container [%d] '%s' started", id, req.name);
        }
        else
        {
            resp.status = 1;
            snprintf(resp.message, sizeof(resp.message),
                     "failed to start container '%s'", req.name);
        }
        break;
    }

    case HK_CMD_PS:
    {
        hk_container_reap();

        char *buf = NULL;
        size_t size = 0;
        FILE *mem = open_memstream(&buf, &size);

        if (mem)
        {
            FILE *old = stdout;
            stdout = mem;
            hk_container_ps();
            stdout = old;
            fclose(mem);
            strncpy(resp.message, buf, sizeof(resp.message) - 1);
            free(buf);
        }
        else
        {
            strncpy(resp.message, "(ps failed)",
                    sizeof(resp.message) - 1);
        }
        resp.status = 0;
        break;
    }

    case HK_CMD_KILL:
    {
        hk_rc_t rc = hk_container_kill(req.kill_id);
        if (rc == HK_OK)
        {
            /*
             * Remove from scheduler queue so it stops
             * getting ticks and we stop sending it signals.
             */
            hk_scheduler_remove(req.kill_id);
            resp.status = 0;
            snprintf(resp.message, sizeof(resp.message),
                     "container [%d] killed", req.kill_id);
        }
        else
        {
            resp.status = 1;
            snprintf(resp.message, sizeof(resp.message),
                     "failed to kill container [%d]", req.kill_id);
        }
        break;
    }

    default:
        resp.status = 1;
        snprintf(resp.message, sizeof(resp.message),
                 "unknown command %d", req.type);
        break;
    }

    hk_send(client_fd, &resp, sizeof(resp));
}

/* ── daemon loop ─────────────────────────────────────────────── */

static void _daemon_loop(int listen_fd)
{
    /*
     * Spawn the scheduler thread.
     * It runs independently, firing ticks every HK_TICK_MS ms.
     * The main thread stays here in the accept() loop.
     */
    pthread_t sched_thread;
    if (pthread_create(&sched_thread, NULL, _scheduler_thread, NULL) != 0)
    {
        HK_ERR("failed to create scheduler thread");
        return;
    }
    HK_INFO("scheduler thread started");

    HK_OK("daemon listening on %s", HK_SOCKET_PATH);
    HK_INFO("waiting for commands...");

    while (1)
    {
        int client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                continue;
            HK_ERR("accept() failed: %s", strerror(errno));
            continue;
        }

        _handle_client(client_fd);
        close(client_fd);
    }
}

/* ── public API ──────────────────────────────────────────────── */

int hk_daemon_start(void)
{
    struct stat st;
    if (stat(HK_SOCKET_PATH, &st) == 0)
    {
        HK_WARN("daemon may already be running");
        HK_WARN("if not: rm %s", HK_SOCKET_PATH);
        return 1;
    }

    int listen_fd = _setup_socket();
    if (listen_fd < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0)
    {
        HK_ERR("fork() failed: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    if (pid > 0)
    {
        /* Parent — return to terminal immediately */
        HK_OK("daemon started  (pid=%d)", pid);
        HK_INFO("socket: %s", HK_SOCKET_PATH);
        close(listen_fd);
        return 0;
    }

    /* Child — we are the daemon */
    setsid();
    hk_container_init();
    hk_scheduler_init(HK_SCHED_PRIORITY); /* use priority scheduling */
    _daemon_loop(listen_fd);

    return 0;
}

int hk_client_send(const hk_request_t *req, hk_response_t *resp)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
    {
        HK_ERR("socket() failed: %s", strerror(errno));
        return -1;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, HK_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        HK_ERR("cannot connect to daemon");
        HK_ERR("is it running?  sudo ./build/hollowkernel daemon");
        close(fd);
        return -1;
    }

    if (hk_send(fd, req, sizeof(*req)) < 0)
    {
        close(fd);
        return -1;
    }
    if (hk_recv(fd, resp, sizeof(*resp)) < 0)
    {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}
