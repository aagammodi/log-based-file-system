
#define _FILE_OFFSET_BITS 64
#define _DEFAULT_SOURCE

#include "logfs.h"

#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition, message)             \
    do {                                      \
        if (!(condition)) {                   \
            fprintf(stderr, "FAIL: %s\n",     \
                    message);                 \
            close(fd);                        \
            return 1;                         \
        }                                     \
    } while (0)

int main(void)
{
    int fd = open("logfs.img", O_RDONLY);

    if (fd < 0) {
        perror("open logfs.img");
        return 1;
    }

    unsigned char block[LOGFS_BLOCK_SIZE];
    struct logfs_super sb;
    struct logfs_checkpoint cp;
    struct logfs_segment_hdr sh;
    struct logfs_inode root;
    struct stat image_stat;

    /* 1. Verify image size. */
    CHECK(fstat(fd, &image_stat) == 0,
          "cannot stat image");

    CHECK((uint64_t)image_stat.st_size == LOGFS_IMAGE_SIZE,
          "incorrect image size");

    /* 2. Read and verify superblock. */
    CHECK(logfs_read_block(fd, 0, block) == 0,
          "cannot read superblock");

    memcpy(&sb, block, sizeof(sb));

    CHECK(sb.magic == LOGFS_MAGIC,
          "incorrect superblock magic");

    CHECK(sb.version == LOGFS_VERSION,
          "incorrect filesystem version");

    CHECK(sb.block_size == LOGFS_BLOCK_SIZE,
          "incorrect block size");

    CHECK(sb.segment_blocks == LOGFS_SEG_BLOCKS,
          "incorrect segment size");

    CHECK(sb.total_blocks == LOGFS_TOTAL_BLOCKS,
          "incorrect total block count");

    CHECK(sb.checkpoint_block == 1,
          "incorrect checkpoint location");

    CHECK(sb.log_start_block == 4,
          "incorrect log start");

    CHECK(sb.crc32 ==
          logfs_crc32(&sb, offsetof(struct logfs_super, crc32)),
          "superblock CRC mismatch");

    /* 3. Read and verify checkpoint. */
    CHECK(pread(fd, &cp, sizeof(cp),
                (off_t)(sb.checkpoint_block *
                        LOGFS_BLOCK_SIZE)) == (ssize_t)sizeof(cp),
          "cannot read checkpoint");

    CHECK(cp.crc32 ==
          logfs_crc32(&cp,
                      offsetof(struct logfs_checkpoint, crc32)),
          "checkpoint CRC mismatch");

    CHECK(cp.imap[0] == sb.log_start_block + 1,
          "root inode mapping is incorrect");

    CHECK(cp.inode_bitmap[0] == 1,
          "root inode is not allocated");

    CHECK(cp.segment_bitmap[0] == 1,
          "initial segment is not allocated");

    for (unsigned int i = 1; i < LOGFS_MAX_INODES; i++) {
        CHECK(cp.imap[i] == 0,
              "unexpected allocated inode mapping");

        CHECK(cp.inode_bitmap[i] == 0,
              "unexpected allocated inode");
    }

    /* 4. Read and verify segment 0 header. */
    CHECK(logfs_read_block(fd, sb.log_start_block, block) == 0,
          "cannot read segment header");

    memcpy(&sh, block, sizeof(sh));

    CHECK(sh.magic == LOGFS_SEG_MAGIC,
          "incorrect segment magic");

    CHECK(sh.seg_seq == 0,
          "incorrect segment sequence");

    CHECK((sh.flags & LOGFS_SEG_SEALED) == 0,
          "initial segment should be open");

    CHECK(sh.crc32 ==
          logfs_crc32(&sh,
                      offsetof(struct logfs_segment_hdr, crc32)),
          "segment header CRC mismatch");

    CHECK(sh.block_map[1] == LOGFS_BLK_INODE,
          "root inode block is not marked as inode");

    for (unsigned int i = 2; i < LOGFS_SEG_BLOCKS; i++) {
        CHECK(sh.block_map[i] == LOGFS_BLK_FREE,
              "unexpected occupied segment slot");
    }

    /* 5. Read and verify root inode. */
    CHECK(logfs_read_block(fd, cp.imap[0], block) == 0,
          "cannot read root inode");

    memcpy(&root, block, sizeof(root));

    CHECK(root.ino == 0,
          "incorrect root inode number");

    CHECK(S_ISDIR(root.mode),
          "root inode is not a directory");

    CHECK(root.size == 0,
          "root directory should initially be empty");

    for (unsigned int i = 0; i < LOGFS_NDIRECT; i++) {
        CHECK(root.direct[i] == 0,
              "root directory should have no data blocks");
    }

    close(fd);

    puts("PASS: image size");
    puts("PASS: superblock and CRC");
    puts("PASS: checkpoint, bitmaps and CRC");
    puts("PASS: segment header and CRC");
    puts("PASS: root inode and empty directory");
    puts("Task A validation successful.");

    return 0;
}
