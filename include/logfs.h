
#ifndef LOGFS_H
#define LOGFS_H

#include <stdint.h>
#include <stddef.h>

/* Filesystem constants specified by the assignment */
#define LOGFS_MAGIC          0x4C4F4746U
#define LOGFS_VERSION        1U
#define LOGFS_BLOCK_SIZE     4096U
#define LOGFS_SEG_BLOCKS     64U
#define LOGFS_MAX_INODES     1024U
#define LOGFS_MAX_SEGMENTS   1024U
#define LOGFS_NDIRECT        12U
#define LOGFS_MAX_NAME       255U
#define LOGFS_IMAGE_SIZE     (256ULL * 1024ULL * 1024ULL)

/* Structure identification constants */
#define LOGFS_SEG_MAGIC      0x5345474DU
#define LOGFS_REC_MAGIC      0x52454352U

/* Segment flags */
#define LOGFS_SEG_SEALED     0x1U

/* Segment block_map values */
#define LOGFS_BLK_FREE       (-1)
#define LOGFS_BLK_INODE      (-2)
#define LOGFS_BLK_REC_HEADER (-3)

/* Log operations */
enum logfs_op {
    OP_CREATE = 1,
    OP_MKDIR  = 2,
    OP_WRITE  = 3,
    OP_RMDIR  = 4
};

/* Block 0: filesystem superblock */
struct logfs_super {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t segment_blocks;
    uint32_t max_inodes;
    uint32_t num_segments;
    uint64_t total_blocks;
    uint64_t checkpoint_block;
    uint64_t log_start_block;
    uint32_t crc32;
};

/* Checkpoint region: persistent allocation metadata */
struct logfs_checkpoint {
    uint64_t imap[LOGFS_MAX_INODES];
    uint8_t inode_bitmap[LOGFS_MAX_INODES];
    uint8_t segment_bitmap[LOGFS_MAX_SEGMENTS];
    uint32_t crc32;
};

/* First block of every segment */
struct logfs_segment_hdr {
    uint32_t magic;
    uint32_t seg_seq;
    uint32_t flags;
    int32_t block_map[LOGFS_SEG_BLOCKS];
    uint32_t crc32;
};

/* One fixed-size directory entry */
struct logfs_dirent {
    uint64_t ino;
    uint16_t name_len;
    char name[LOGFS_MAX_NAME];
};

/* One inode occupies one complete disk block */
struct logfs_inode {
    uint64_t ino;
    uint32_t mode;
    uint64_t size;
    int64_t mtime;
    uint64_t direct[LOGFS_NDIRECT];
};

/* One-block log-record header */
struct logfs_rec_hdr {
    uint32_t magic;
    uint32_t op;
    uint32_t nblocks;
    uint32_t crc32;
};

/* Derived layout constants */
#define LOGFS_TOTAL_BLOCKS \
    (LOGFS_IMAGE_SIZE / LOGFS_BLOCK_SIZE)

#define LOGFS_CHECKPOINT_BLOCKS \
    ((sizeof(struct logfs_checkpoint) + LOGFS_BLOCK_SIZE - 1U) \
        / LOGFS_BLOCK_SIZE)

int logfs_read_block(int fd, uint64_t block_no, void *buf);
int logfs_write_block(int fd, uint64_t block_no, const void *buf);

/* CRC32 helper shared by formatter and filesystem library */
uint32_t logfs_crc32(const void *data, size_t len);

#endif
