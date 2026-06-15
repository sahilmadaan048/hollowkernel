#ifndef HK_SCHEDULER_H
#define HK_SCHEDULER_H

/*
 * hk_scheduler.h — public API for the hollowkernel scheduler
 *
 * The scheduler manages CPU time distribution across running
 * containers using SIGSTOP/SIGCONT signals.
 *
 * Two algorithms available:
 *   HK_SCHED_RR       — round robin (equal time slices)
 *   HK_SCHED_PRIORITY — priority based (higher pri = more turns)
 */

#include "hk_types.h"

/* ── scheduling algorithms ───────────────────────────────────── */
typedef enum
{
    HK_SCHED_RR = 0,       /* round robin — default            */
    HK_SCHED_PRIORITY = 1, /* priority based                   */
} hk_sched_algo_t;

/* How long each scheduling tick is in milliseconds */
#define HK_TICK_MS 500

/*
 * Initialise the scheduler.
 * Call once at daemon startup, before any containers are spawned.
 */
void hk_scheduler_init(hk_sched_algo_t algo);

/*
 * Add a container to the run queue.
 * Called by the container manager after a successful spawn.
 */
void hk_scheduler_add(int container_id);

/*
 * Remove a container from the run queue.
 * Called when a container is killed or exits.
 */
void hk_scheduler_remove(int container_id);

/*
 * Run one scheduling tick.
 *
 * Picks the next container to run, SIGSTOPs all others,
 * SIGCONTs the chosen one.
 *
 * Called periodically by the daemon's scheduler thread.
 */
void hk_scheduler_tick(void);

/*
 * Print the current run queue state to stdout.
 */
void hk_scheduler_ps(void);

#endif /* HK_SCHEDULER_H */