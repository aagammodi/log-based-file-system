
#define _FILE_OFFSET_BITS 64
#define _DEFAULT_SOURCE

#include "logfs.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int fd = open("logfs.img", O_RDONLY);

    if (fd < 0) {
        perror("open logfs.img");
        return 1;
    }

    struct logfs_super sb;

    ssize_t n = pread(fd, &sb, sizeof(sb), 0);
    close(fd);

    if (n != (ssize_t)sizeof(sb)) {
        fprintf(stderr, "FAIL: could not read superblock\n");
        return 1;
    }

    /* Simulate corruption of a protected superblock field. */
    sb.version ^= 1U;

    uint32_t calculated_crc =
        logfs_crc32(&sb, offsetof(struct logfs_super, crc32));

    if (calculated_crc == sb.crc32) {
        fprintf(stderr, "FAIL: corruption was not detected\n");
        return 1;
    }

    puts("PASS: superblock corruption detected");
    return 0;
}
