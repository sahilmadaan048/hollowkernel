#ifndef HK_DAEMON_H
#define HK_DAEMON_H

/*
 * hk_daemon.h — public API for the daemon module
 *
 * The daemon is a long-running background process that:
 *   - owns the container table in memory
 *   - listens on a Unix socket for commands
 *   - spawns/kills/lists containers on behalf of the CLI client
 *
 * Only two functions are public:
 *   hk_daemon_start() — fork off the daemon and return
 *   hk_client_send()  — send a request, get a response
 */

#include "hk_socket.h"

/*
 * Fork a background daemon process that listens on HK_SOCKET_PATH.
 *
 * The parent returns immediately (so the terminal is free).
 * The child enters an infinite accept() loop.
 *
 * Returns  0 on success (in the parent).
 * Returns -1 if the fork or socket setup fails.
 * Returns  1 if a daemon is already running.
 */
int hk_daemon_start(void);

/*
 * Send a request to the running daemon and receive a response.
 *
 * Connects to HK_SOCKET_PATH, sends `req`, reads back `resp`.
 *
 * Returns  0 on success.
 * Returns -1 if the daemon isn't running or connection fails.
 */
int hk_client_send(const hk_request_t *req, hk_response_t *resp);

#endif /* HK_DAEMON_H */