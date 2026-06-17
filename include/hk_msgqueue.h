#ifndef HK_MSGQUEUE_H
#define HK_MSGQUEUE_H

/*
 * hk_msgqueue.h — POSIX message queue IPC between containers
 *
 * Unlike shared memory (a whiteboard anyone can scribble on),
 * a message queue is a mailbox:
 *   - messages are discrete and ordered
 *   - mq_receive() blocks until a message is available
 *   - each message has a priority (higher priority delivered first)
 *
 * Queue names must start with '/' per POSIX convention, e.g. "/hk-mq1"
 */

#include <stddef.h>

#define HK_MQ_MAX_MSG_SIZE 1024 /* max bytes per message      */
#define HK_MQ_MAX_MESSAGES 10   /* max messages queued at once */

/*
 * Open (creating if necessary) a message queue and send one message.
 *
 *   name     — queue name, MUST start with '/' e.g. "/hk-orders"
 *   data     — message bytes to send
 *   len      — length of data (must be <= HK_MQ_MAX_MSG_SIZE)
 *   priority — 0 (lowest) to 31 (highest) — higher delivered first
 *
 * Returns 0 on success, -1 on failure.
 */
int hk_mq_send(const char *name, const void *data, size_t len,
               unsigned int priority);

/*
 * Open (creating if necessary) a message queue and receive one message.
 * BLOCKS until a message is available.
 *
 *   name    — queue name to receive from
 *   out_buf — caller-provided buffer to receive into
 *   buf_len — size of out_buf (should be >= HK_MQ_MAX_MSG_SIZE)
 *
 * Returns number of bytes received on success, -1 on failure.
 */
int hk_mq_receive(const char *name, void *out_buf, size_t buf_len);

/*
 * Destroy a message queue permanently.
 * Returns 0 on success, -1 on failure.
 */
int hk_mq_destroy(const char *name);

#endif /* HK_MSGQUEUE_H */