#ifndef HK_CONTAINER_H
#define HK_CONTAINER_H

/*
 * hk_container.h — public API for the container manager
 *
 * This header is what other files include when they want to
 * interact with containers. The actual implementation lives
 * in src/container/container.c
 *
 * Rule: NEVER put function bodies in headers. Only declarations.
 */

#include "hk_types.h"

/*
 * Call once at startup.
 * Zeroes out the container table so all slots start as EMPTY.
 */
void hk_container_init(void);

/*
 * Spawn a new isolated container.
 *
 *   name     — human label, shows up in `ps` output
 *   cmd      — argv[] to exec inside (NULL-terminated, just like main's argv)
 *   priority — 0 (lowest) to 9 (highest)
 *
 * Returns the container id (>0) on success, HK_ERR on failure.
 *
 * Internally this calls clone() with namespace flags — not fork().
 * That's the key difference from a normal process spawn.
 */
int hk_container_run(const char *name, char *const cmd[], int priority);

/*
 * Send SIGKILL to the container with the given id.
 * Updates state to HK_STATE_STOPPED.
 */
hk_rc_t hk_container_kill(int id);

/*
 * Print a formatted table of all containers to stdout.
 * Similar to `docker ps` or `ps aux`.
 */
void hk_container_ps(void);

/*
 * Look up a container by id.
 * Returns a pointer into the table, or NULL if not found.
 * Caller must NOT free this pointer — we own the memory.
 */
hk_container_t *hk_container_get(int id);

/*
 * Find container processes that have exited, collect their exit status with waitpid(), remove zombies, and update their state from RUNNING to STOPPED
 * Non-blocking reap of any containers that have exited.
 * Calls waitpid(WNOHANG) internally.
 * Call this before ps() to get accurate state.
 */
void hk_container_reap(void);

/*
 * Returns count of containers currently in RUNNING state.
 */
int hk_container_count(void);

#endif /* HK_CONTAINER_H */