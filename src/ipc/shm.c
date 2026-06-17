/*
 * shm.c — shared memory IPC implementation
 *
 * Uses POSIX shared memory (shm_open + mmap) to let containers
 * exchange data through named segments under /dev/shm.
 */

#include <fcntl.h>        /* O_CREAT, O_RDWR, O_RDONLY  */
#include <sys/mman.h>     /* mmap(), munmap(), shm_open() */
#include <sys/stat.h>     /* mode constants              */
#include <unistd.h>       /* ftruncate(), close()        */
#include <string.h>       /* memcpy(), strerror()        */
#include <errno.h>

#include "hk_ipc.h"
#include "hk_log.h"

int hk_shm_write(const char *name, const void *data, size_t len)
{
    if (len > HK_SHM_MAX_SIZE) {
        HK_ERR("shm_write: data too large (%zu > %d)", len, HK_SHM_MAX_SIZE);
        return -1;
    }

    /*
     * shm_open creates (or opens) a POSIX shared memory object.
     * It shows up as a file under /dev/shm/<name>.
     *
     * O_CREAT  — create if it doesn't exist
     * O_RDWR   — we need to write to it
     * 0666     — permissions (rw for everyone, simplest for now)
     */
    int fd = shm_open(name, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        HK_ERR("shm_open('%s') failed: %s", name, strerror(errno));
        return -1;
    }

    /*
     * ftruncate sets the size of the shared memory object.
     * Without this, mmap would map a zero-length region.
     */
    if (ftruncate(fd, HK_SHM_MAX_SIZE) < 0) {
        HK_ERR("ftruncate failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    /*
     * mmap maps the shared memory into OUR address space.
     *
     *   NULL            — let the kernel choose the address
     *   HK_SHM_MAX_SIZE — how many bytes to map
     *   PROT_WRITE      — we intend to write
     *   MAP_SHARED      — changes are visible to other processes
     *                      mapping the SAME segment
     *   fd, 0           — the fd from shm_open, offset 0
     */
    void *ptr = mmap(NULL, HK_SHM_MAX_SIZE,
                      PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    if (ptr == MAP_FAILED) {
        HK_ERR("mmap failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    /* Copy our data into the mapped memory */
    memcpy(ptr, data, len);

    /*
     * Unmap from OUR address space — the data stays alive in
     * /dev/shm because the underlying object persists until
     * explicitly unlinked via hk_shm_destroy().
     */
    munmap(ptr, HK_SHM_MAX_SIZE);
    close(fd);

    HK_OK("shm: wrote %zu bytes to '%s'", len, name);
    return 0;
}

int hk_shm_read(const char *name, void *out_buf, size_t buf_len)
{
    /* O_RDONLY — we only intend to read this time */
    int fd = shm_open(name, O_RDONLY, 0666);
    if (fd < 0) {
        HK_ERR("shm_open('%s') failed: %s — does it exist?",
               name, strerror(errno));
        return -1;
    }

    void *ptr = mmap(NULL, HK_SHM_MAX_SIZE,
                      PROT_READ, MAP_SHARED, fd, 0);

    if (ptr == MAP_FAILED) {
        HK_ERR("mmap failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    size_t to_copy = (buf_len < HK_SHM_MAX_SIZE) ? buf_len : HK_SHM_MAX_SIZE;
    memcpy(out_buf, ptr, to_copy);

    munmap(ptr, HK_SHM_MAX_SIZE);
    close(fd);

    HK_OK("shm: read %zu bytes from '%s'", to_copy, name);
    return (int)to_copy;
}

int hk_shm_destroy(const char *name)
{
    /*
     * shm_unlink removes the name from /dev/shm.
     * The memory is freed once all processes that have it
     * mapped close their mappings.
     */
    if (shm_unlink(name) < 0) {
        HK_ERR("shm_unlink('%s') failed: %s", name, strerror(errno));
        return -1;
    }

    HK_OK("shm: destroyed segment '%s'", name);
    return 0;
}