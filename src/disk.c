#define _DEFAULT_SOURCE
#define _FILE_OFFSET_BITS 64

#include "logfs.h"

#include <errno.h>
#include <unistd.h>

/*
 * Read exactly len bytes, handling interrupted and partial reads.
 * Returns 0 on success and -1 on failure.
 */
static int read_exact(int fd, void *buf, size_t len, off_t offset)
{
    unsigned char *p = buf;
    size_t done = 0;

    while (done < len) {
        ssize_t n = pread(fd, p + done, len - done,
                          offset + (off_t)done);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0) {
            errno = EIO;
            return -1;
        }

        done += (size_t)n;
    }

    return 0;
}

/*
 * Write exactly len bytes, handling interrupted and partial writes.
 * Returns 0 on success and -1 on failure.
 */
static int write_exact(int fd, const void *buf, size_t len,
                       off_t offset)
{
    const unsigned char *p = buf;
    size_t done = 0;

    while (done < len) {
        ssize_t n = pwrite(fd, p + done, len - done,
                           offset + (off_t)done);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }

        if (n == 0) {
            errno = EIO;
            return -1;
        }

        done += (size_t)n;
    }

    return 0;
}

int logfs_read_block(int fd, uint64_t block_no, void *buf)
{
    if (block_no >= LOGFS_TOTAL_BLOCKS) {
        errno = EINVAL;
        return -1;
    }

    return read_exact(
        fd, buf, LOGFS_BLOCK_SIZE,
        (off_t)(block_no * (uint64_t)LOGFS_BLOCK_SIZE)
    );
}

int logfs_write_block(int fd, uint64_t block_no, const void *buf)
{
    if (block_no >= LOGFS_TOTAL_BLOCKS) {
        errno = EINVAL;
        return -1;
    }

    return write_exact(
        fd, buf, LOGFS_BLOCK_SIZE,
        (off_t)(block_no * (uint64_t)LOGFS_BLOCK_SIZE)
    );
}
