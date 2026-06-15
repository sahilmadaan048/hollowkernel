/*
 * socket.c — shared socket helpers for hollowkernel IPC
 *
 * Two functions only:
 *   hk_send() — reliably send exactly `len` bytes
 *   hk_recv() — reliably receive exactly `len` bytes
 *
 * Both are used by the daemon (server side) and the CLI (client side).
 * They live here so there's exactly one copy of this logic.
 */

#include <unistd.h> /* read(), write()  */
#include <stddef.h> /* size_t           */

#include "hk_socket.h"
#include "hk_log.h"

/*
 * hk_send — send exactly `len` bytes from `buf` over `fd`
 *
 * Loops until all bytes are written or an error occurs.
 * Returns 0 on success, -1 on error.
 */
int hk_send(int fd, const void *buf, size_t len)
{
    size_t sent = 0;
    const char *ptr = (const char *)buf;

    while (sent < len)
    {
        /*
         * write() returns:
         *   >0  number of bytes actually written (may be less than len)
         *    0  nothing written (shouldn't happen on a socket)
         *   -1  error
         */
        ssize_t n = write(fd, ptr + sent, len - sent);
        if (n <= 0)
        {
            HK_ERR("hk_send: write() failed");
            return -1;
        }
        sent += (size_t)n;
    }

    return 0;
}

/*
 * hk_recv — receive exactly `len` bytes into `buf` from `fd`
 *
 * Loops until all bytes are read or connection is closed/errors.
 * Returns 0 on success, -1 on error or connection closed early.
 */
int hk_recv(int fd, void *buf, size_t len)
{
    size_t recvd = 0;
    char *ptr = (char *)buf;

    while (recvd < len)
    {
        /*
         * read() returns:
         *   >0  bytes read
         *    0  connection closed by other side (EOF)
         *   -1  error
         */
        ssize_t n = read(fd, ptr + recvd, len - recvd);
        if (n <= 0)
        {
            HK_ERR("hk_recv: read() failed or connection closed");
            return -1;
        }
        recvd += (size_t)n;
    }

    return 0;
}