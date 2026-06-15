#ifndef HK_SOCKET_H
#define HK_SOCKET_H

/*
 * hk_socket.h — IPC protocol between hollowkernel client and daemon
 *
 * The client and daemon talk over a Unix domain socket.
 * Both sides include this header so they agree on:
 *   - where the socket lives
 *   - what a request looks like
 *   - what a response looks like
 *
 * We use fixed-size structs and send raw bytes — no parsing needed.
 */

#include <stdint.h>

/* Path to the Unix domain socket file.
 * The daemon creates this. The client connects to it. */
#define HK_SOCKET_PATH "/tmp/hollowkernel.sock"

/* Maximum number of argv tokens in a run command */
#define HK_MAX_ARGS 32

/* Maximum length of each arg string */
#define HK_ARG_MAX 128

/* ── command types
 *
 * Every request from client → daemon has one of these types.
 * The daemon reads the type first and knows what to do.
 * */
typedef enum
{
    HK_CMD_RUN = 1,
    HK_CMD_PS = 2,
    HK_CMD_KILL = 3,
} hk_cmd_type_t;

/* ── request struct
 *
 * Client fills this and sends it to the daemon.
 * Fixed size — daemon knows exactly how many bytes to read.
 *  */
typedef struct
{
    hk_cmd_type_t type;                 /* which command       */
    char name[64];                      /* container name      */
    int priority;                       /* 0-9                 */
    int kill_id;                        /* used by CMD_KILL    */
    int argc;                           /* number of cmd args  */
    char argv[HK_MAX_ARGS][HK_ARG_MAX]; /* the command + args  */
} hk_request_t;

/* response struct
 *
 * Daemon fills this and sends it back to the client.
 *  */
typedef struct
{
    int status;         /* 0 = success, non-zero = error       */
    char message[2048]; /* human-readable output (ps table etc)*/
} hk_response_t;

/* helper function declarations
 *
 * Implemented in src/ipc/socket.c
 * Used by both daemon and client.
 * */

/* Send exactly `len` bytes — retries on partial writes */
int hk_send(int fd, const void *buf, size_t len);

/* Receive exactly `len` bytes — retries on partial reads */
int hk_recv(int fd, void *buf, size_t len);

#endif /* HK_SOCKET_H */