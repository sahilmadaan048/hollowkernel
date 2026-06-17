/*
 * msgqueue.c — POSIX message queue IPC implementation
 *
 * A message queue is a kernel-managed mailbox. Messages are
 * discrete, ordered by priority, and mq_receive() blocks until
 * one is available — fundamentally different from shared memory
 * where reads never block and there's no message boundary.
 */

#include <fcntl.h>  /* O_CREAT, O_RDWR              */
#include <mqueue.h> /* mqd_t, mq_open, mq_send, ... */
#include <string.h> /* strerror()                   */
#include <errno.h>

#include "hk_msgqueue.h"
#include "hk_log.h"

/*
 * _open_queue — shared helper to open/create a queue with
 * consistent attributes. Used by both send and receive.
 */
static mqd_t _open_queue(const char *name)
{
    /*
     * struct mq_attr fields:
     *   mq_flags   — 0 for blocking mode (default)
     *   mq_maxmsg  — max number of messages the queue can hold
     *   mq_msgsize — max size of a single message in bytes
     *   mq_curmsgs — current count (read-only, kernel sets this)
     *
     * NOTE: these only apply when CREATING the queue. If it
     * already exists, the existing attributes win and these
     * are ignored by the kernel.
     */
    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_flags = 0;
    attr.mq_maxmsg = HK_MQ_MAX_MESSAGES;
    attr.mq_msgsize = HK_MQ_MAX_MSG_SIZE;

    mqd_t mq = mq_open(name, O_CREAT | O_RDWR, 0666, &attr);
    if (mq == (mqd_t)-1)
        HK_ERR("mq_open('%s') failed: %s", name, strerror(errno));

    return mq;
}

int hk_mq_send(const char *name, const void *data, size_t len,
               unsigned int priority)
{
    if (len > HK_MQ_MAX_MSG_SIZE)
    {
        HK_ERR("mq_send: message too large (%zu > %d)",
               len, HK_MQ_MAX_MSG_SIZE);
        return -1;
    }

    mqd_t mq = _open_queue(name);
    if (mq == (mqd_t)-1)
        return -1;

    /*
     * mq_send(queue, data, len, priority)
     *
     * Higher priority value = delivered before lower priority
     * messages, regardless of send order. Two messages with the
     * SAME priority are delivered FIFO relative to each other.
     */
    if (mq_send(mq, data, len, priority) < 0)
    {
        HK_ERR("mq_send failed: %s", strerror(errno));
        mq_close(mq);
        return -1;
    }

    mq_close(mq);
    HK_OK("mq: sent %zu bytes to '%s'  (priority=%u)", len, name, priority);
    return 0;
}

int hk_mq_receive(const char *name, void *out_buf, size_t buf_len)
{
    mqd_t mq = _open_queue(name);
    if (mq == (mqd_t)-1)
        return -1;

    unsigned int priority = 0;

    /*
     * mq_receive BLOCKS here if the queue is empty — the calling
     * process sleeps until another process calls mq_send() on
     * the same queue name. This is the key behavioral difference
     * from shared memory reads.
     */
    HK_INFO("mq: waiting for a message on '%s'...", name);

    ssize_t n = mq_receive(mq, out_buf, buf_len, &priority);
    if (n < 0)
    {
        HK_ERR("mq_receive failed: %s", strerror(errno));
        mq_close(mq);
        return -1;
    }

    mq_close(mq);
    HK_OK("mq: received %zd bytes from '%s'  (priority=%u)",
          n, name, priority);
    return (int)n;
}

int hk_mq_destroy(const char *name)
{
    /*
     * mq_unlink removes the queue's name. Like shm_unlink,
     * actual cleanup happens once all open handles are closed.
     */
    if (mq_unlink(name) < 0)
    {
        HK_ERR("mq_unlink('%s') failed: %s", name, strerror(errno));
        return -1;
    }

    HK_OK("mq: destroyed queue '%s'", name);
    return 0;
}