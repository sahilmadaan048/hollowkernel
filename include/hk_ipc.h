#ifndef HK_IPC_H
#define HK_IPC_H

/*
 * hk_ipc.h — shared memory IPC between containers
 *
 * Containers communicate through named shared memory segments
 * living under /dev/shm. Any container that knows the segment
 * name can attach to it and read/write.
 *
 * This is deliberately NOT isolated by the mount namespace —
 * /dev/shm is the one place containers can see each other on
 * purpose, so they have a channel to communicate through.
 */

#include <stddef.h>

/* Max size of a single shared memory segment (in bytes) */
#define HK_SHM_MAX_SIZE   4096

/* Max length of a segment name */
#define HK_SHM_NAME_MAX   64

/*
 * Create (or open if it already exists) a shared memory segment
 * and write `data` into it.
 *
 *   name — segment name, becomes /dev/shm/<name>
 *   data — bytes to write
 *   len  — length of data (must be <= HK_SHM_MAX_SIZE)
 *
 * Returns 0 on success, -1 on failure.
 */
int hk_shm_write(const char *name, const void *data, size_t len);

/*
 * Open an existing shared memory segment and read its contents.
 *
 *   name    — segment name to open
 *   out_buf — caller-provided buffer to read into
 *   buf_len — size of out_buf
 *
 * Returns number of bytes read on success, -1 on failure.
 */
int hk_shm_read(const char *name, void *out_buf, size_t buf_len);

/*
 * Destroy a shared memory segment.
 * Returns 0 on success, -1 if it didn't exist or failed.
 */
int hk_shm_destroy(const char *name);

#endif /* HK_IPC_H */