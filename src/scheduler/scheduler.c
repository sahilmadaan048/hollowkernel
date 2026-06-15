/*
 * scheduler.c — CPU time distribution via SIGSTOP/SIGCONT
 *
 * Maintains a run queue of container IDs.
 * Each tick: freeze all containers except the chosen one.
 *
 * Two algorithms:
 *   RR       — advance index by 1 each tick (equal time)
 *   PRIORITY — build a weighted queue where higher priority
 *              containers appear more times
 */

#include <signal.h> /* kill(), SIGSTOP, SIGCONT  */
#include <stdio.h>
#include <string.h>
#include <unistd.h> /* usleep()                  */

#include "hk_scheduler.h"
#include "hk_container.h"
#include "hk_log.h"

/* ── run queue
 *
 * For RR: each container id appears exactly once.
 * For PRIORITY: a container with priority N appears N+1 times.
 * Max slots = MAX_CONTAINERS * 10 (max priority slots per container)
 *  */
#define HK_QUEUE_MAX (HK_MAX_CONTAINERS * 10)

static int _queue[HK_QUEUE_MAX]; /* container ids    */
static int _queue_len = 0;       /* active slots     */
static int _current = 0;         /* current index    */
static hk_sched_algo_t _algo = HK_SCHED_RR;

/*
 * _rebuild_queue — reconstruct the run queue from scratch
 *
 * For RR: one slot per container regardless of priority.
 * For PRIORITY: (priority + 1) slots per container.
 *   priority 0 → 1 slot
 *   priority 5 → 6 slots
 *   priority 9 → 10 slots
 *
 * This means in a tick cycle, a priority-9 container gets
 * 10x more turns than a priority-0 container.
 */
static void _rebuild_queue(void)
{
    _queue_len = 0;
    _current = 0;

    for (int i = 0; i < HK_MAX_CONTAINERS; i++)
    {
        hk_container_t *c = hk_container_get(i + 1); /* ids start at 1 */
        if (!c || c->state != HK_STATE_RUNNING)
            continue;

        int slots = (_algo == HK_SCHED_PRIORITY)
                        ? (c->priority + 1)
                        : 1;

        for (int s = 0; s < slots && _queue_len < HK_QUEUE_MAX; s++)
            _queue[_queue_len++] = c->id;
    }
}

/* public API */
void hk_scheduler_init(hk_sched_algo_t algo)
{
    _algo = algo;
    _queue_len = 0;
    _current = 0;
    memset(_queue, 0, sizeof(_queue));

    const char *name = (algo == HK_SCHED_RR) ? "Round Robin" : "Priority";
    HK_INFO("scheduler initialised  algorithm=%s  tick=%dms",
            name, HK_TICK_MS);
}

void hk_scheduler_add(int container_id)
{
    /*
     * Rather than inserting into the queue manually,
     * we rebuild it from the container table.
     * Simpler and always consistent.
     */
    (void)container_id; /* used implicitly via _rebuild_queue */
    _rebuild_queue();
    HK_INFO("scheduler: queue rebuilt (%d slots)", _queue_len);
}

void hk_scheduler_remove(int container_id)
{
    (void)container_id;
    _rebuild_queue();
    HK_INFO("scheduler: queue rebuilt after removal (%d slots)", _queue_len);
}

void hk_scheduler_tick(void)
{
    if (_queue_len == 0)
        return; /* nothing to schedule */

    /* Wrap around if we've reached the end of the queue */
    if (_current >= _queue_len)
        _current = 0;

    int chosen_id = _queue[_current];
    _current++;

    hk_container_t *chosen = hk_container_get(chosen_id);
    if (!chosen || chosen->state != HK_STATE_RUNNING)
    {
        /* Stale entry — rebuild and skip this tick */
        _rebuild_queue();
        return;
    }

    /*
     * Freeze all running containers EXCEPT the chosen one.
     * SIGSTOP suspends a process — it won't get any CPU time
     * until we send SIGCONT.
     */
    for (int i = 0; i < _queue_len; i++)
    {
        int id = _queue[i];
        if (id == chosen_id)
            continue;

        hk_container_t *c = hk_container_get(id);
        if (c && c->state == HK_STATE_RUNNING)
            kill(c->pid, SIGSTOP);
    }

    /* Resume the chosen container */
    kill(chosen->pid, SIGCONT);

    HK_INFO("scheduler: tick → container [%d] '%s'  (pid=%d)",
            chosen->id, chosen->name, chosen->pid);
}

void hk_scheduler_ps(void)
{
    printf("\n  \033[1mScheduler state\033[0m\n");
    printf("  algorithm : %s\n",
           (_algo == HK_SCHED_RR) ? "Round Robin" : "Priority");
    printf("  queue len : %d\n", _queue_len);
    printf("  current   : %d\n\n", _current);

    printf("  \033[1m%-4s  %-20s  %s\033[0m\n", "SLOT", "CONTAINER ID", "");
    for (int i = 0; i < _queue_len; i++)
    {
        const char *marker = (i == _current) ? " ← next" : "";
        printf("  %-4d  %-20d  %s\n", i, _queue[i], marker);
    }
    printf("\n");
}