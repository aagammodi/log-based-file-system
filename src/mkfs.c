#define _DEFAULT_SOURCE
#define _FILE_OFFSET_BITS 64

#include "logfs.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int write_struct_region(
    int fd,
    uint64_t start_block,
    const void *data,
    size_t data_size,
    size_t region_blocks)
{
    unsigned char block[LOGFS_BLOCK_SIZE];
    const unsigned char *src = data;

    for (size_t i = 0; i < region_blocks; i++) {
        memset(block, 0, sizeof(block));

        size_t offset = i * LOGFS_BLOCK_SIZE;
        if (offset < data_size) {
            size_t amount = data_size - offset;

            if (amount > LOGFS_BLOCK_SIZE)
                amount = LOGFS_BLOCK_SIZE;

            memcpy(block, src + offset, amount);
        }

        if (logfs_write_block(fd, start_block + i, block) < 0)
            return -1;
    }

    return 0;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s [image-path]\n", program);
    fprintf(stderr, "Default image path: logfs.img\n");
}

int main(int argc, char **argv)
{
    const char *image_path = "logfs.img";
    int fd;
    struct logfs_super sb;
    struct logfs_checkpoint cp;
    struct logfs_segment_hdr sh;
    struct logfs_inode root;
    unsigned char block[LOGFS_BLOCK_SIZE];

    const uint64_t total_blocks = LOGFS_TOTAL_BLOCKS;
    const uint64_t checkpoint_block = 1;
    const uint64_t log_start_block =
        checkpoint_block + LOGFS_CHECKPOINT_BLOCKS;

    const uint64_t remaining_blocks =
        total_blocks - log_start_block;

    const uint32_t num_segments =
        (uint32_t)(remaining_blocks / LOGFS_SEG_BLOCKS);

    if (argc > 2) {
        usage(argv[0]);
        return 2;
    }

    if (argc == 2)
        image_path = argv[1];

    if (sizeof(sb) > LOGFS_BLOCK_SIZE ||
        sizeof(sh) > LOGFS_BLOCK_SIZE ||
        sizeof(root) > LOGFS_BLOCK_SIZE) {
        fprintf(stderr, "Error: an on-disk structure exceeds one block\n");
        return 1;
    }

    if (num_segments == 0 ||
        num_segments > LOGFS_MAX_SEGMENTS) {
        fprintf(stderr, "Error: invalid segment count\n");
        return 1;
    }

    /*
     * O_TRUNC is intentional: formatting destroys any old image.
     */
    fd = open(image_path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        perror("open image");
        return 1;
    }

    if (ftruncate(fd, (off_t)LOGFS_IMAGE_SIZE) < 0) {
        perror("ftruncate");
        close(fd);
        return 1;
    }

    /* ---------------------------------------------------------
     * 1. Construct the superblock.
     * --------------------------------------------------------- */
    memset(&sb, 0, sizeof(sb));

    sb.magic = LOGFS_MAGIC;
    sb.version = LOGFS_VERSION;
    sb.block_size = LOGFS_BLOCK_SIZE;
    sb.segment_blocks = LOGFS_SEG_BLOCKS;
    sb.max_inodes = LOGFS_MAX_INODES;
    sb.num_segments = num_segments;
    sb.total_blocks = total_blocks;
    sb.checkpoint_block = checkpoint_block;
    sb.log_start_block = log_start_block;

    sb.crc32 = logfs_crc32(
        &sb, offsetof(struct logfs_super, crc32));

    memset(block, 0, sizeof(block));
    memcpy(block, &sb, sizeof(sb));

    if (logfs_write_block(fd, 0, block) < 0) {
        perror("write superblock");
        close(fd);
        return 1;
    }

    /* ---------------------------------------------------------
     * 2. Construct the initial checkpoint.
     * --------------------------------------------------------- */
    memset(&cp, 0, sizeof(cp));

    /*
     * Segment 0 starts with its header.
     * The root inode occupies body slot 1.
     */
    const uint64_t root_inode_block = log_start_block + 1;

    cp.imap[0] = root_inode_block;
    cp.inode_bitmap[0] = 1;
    cp.segment_bitmap[0] = 1;

    cp.crc32 = logfs_crc32(
        &cp, offsetof(struct logfs_checkpoint, crc32));

    if (write_struct_region(
            fd, checkpoint_block, &cp, sizeof(cp),
            LOGFS_CHECKPOINT_BLOCKS) < 0) {
        perror("write checkpoint");
        close(fd);
        return 1;
    }

    /* ---------------------------------------------------------
     * 3. Construct segment 0's header.
     * --------------------------------------------------------- */
    memset(&sh, 0, sizeof(sh));

    sh.magic = LOGFS_SEG_MAGIC;
    sh.seg_seq = 0;
    sh.flags = 0; /* open, not sealed */

    for (unsigned int i = 0; i < LOGFS_SEG_BLOCKS; i++)
        sh.block_map[i] = LOGFS_BLK_FREE;

    /*
     * Segment-relative index 0 is the header itself.
     * Index 1 contains the root inode.
     */
    sh.block_map[0] = LOGFS_BLK_FREE;
    sh.block_map[1] = LOGFS_BLK_INODE;

    sh.crc32 = logfs_crc32(
        &sh, offsetof(struct logfs_segment_hdr, crc32));

    memset(block, 0, sizeof(block));
    memcpy(block, &sh, sizeof(sh));

    if (logfs_write_block(fd, log_start_block, block) < 0) {
        perror("write segment header");
        close(fd);
        return 1;
    }

    /* ---------------------------------------------------------
     * 4. Construct and write the empty root directory inode.
     * --------------------------------------------------------- */
    memset(&root, 0, sizeof(root));

    root.ino = 0;
    root.mode = (uint32_t)(S_IFDIR | 0755);
    root.size = 0;
    root.mtime = (int64_t)time(NULL);

    /* All 12 direct pointers remain zero: empty directory. */

    memset(block, 0, sizeof(block));
    memcpy(block, &root, sizeof(root));

    if (logfs_write_block(fd, root_inode_block, block) < 0) {
        perror("write root inode");
        close(fd);
        return 1;
    }

    /* Ensure all formatter writes reach the backing image. */
    if (fsync(fd) < 0) {
        perror("fsync image");
        close(fd);
        return 1;
    }

    if (close(fd) < 0) {
        perror("close image");
        return 1;
    }

    printf("Formatted %s successfully\n", image_path);
    printf("Image size:        %llu bytes\n",
           (unsigned long long)LOGFS_IMAGE_SIZE);
    printf("Total blocks:      %llu\n",
           (unsigned long long)total_blocks);
    printf("Checkpoint start:  %llu\n",
           (unsigned long long)checkpoint_block);
    printf("Checkpoint blocks: %u\n",
           (unsigned int)LOGFS_CHECKPOINT_BLOCKS);
    printf("Log start:         %llu\n",
           (unsigned long long)log_start_block);
    printf("Log segments:      %u\n", num_segments);
    printf("Root inode block:  %llu\n",
           (unsigned long long)root_inode_block);

    return 0;
}
