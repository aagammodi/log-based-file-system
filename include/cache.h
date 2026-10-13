
#ifndef LOGFS_CACHE_H
#define LOGFS_CACHE_H

#include "logfs.h"

#include <stdint.h>

/* Required cache capacities from Task B. */
#define LOGFS_DATA_CACHE_ENTRIES  64U
#define LOGFS_INODE_CACHE_ENTRIES 64U

/*
 * Data cache:
 * One cached filesystem data block.
 */
struct logfs_data_cache_entry {
    uint64_t block_no;
    uint8_t data[LOGFS_BLOCK_SIZE];
    uint64_t last_used;
    int valid;
    int dirty;
};

/*
 * Inode cache:
 * One cached inode, indexed by inode number.
 */
struct logfs_inode_cache_entry {
    uint64_t ino;
    struct logfs_inode inode;
    uint64_t last_used;
    int valid;
    int dirty;
};

/*
 * Exactly one segment buffer.
 * Its byte array is exactly 64 * 4096 = 262144 bytes.
 *
 * Block 0 of this array holds the segment header.
 * Remaining blocks hold log records and their payloads.
 */
struct logfs_segment_buffer {
    uint8_t bytes[LOGFS_SEG_BLOCKS][LOGFS_BLOCK_SIZE];

    /* Metadata about the in-memory buffer, not extra segment storage. */
    uint64_t segment_start_block;
    uint32_t next_slot;
    int dirty;
};

/*
 * All in-memory filesystem caches.
 *
 * There is exactly one checkpoint and one superblock here.
 * The segment buffer is also singular.
 */
struct logfs_cache {
    struct logfs_data_cache_entry
        data[LOGFS_DATA_CACHE_ENTRIES];

    struct logfs_inode_cache_entry
        inodes[LOGFS_INODE_CACHE_ENTRIES];

    struct logfs_checkpoint checkpoint;
    struct logfs_super superblock;

    struct logfs_segment_buffer segment;

    /* Shared logical clock used by the LRU eviction policy. */
    uint64_t access_clock;
};

/* Cache initialization. */
void logfs_cache_init(struct logfs_cache *cache);

/* Data-block cache operations. */
int logfs_cache_get_data(
    struct logfs_cache *cache,
    uint64_t block_no,
    uint8_t out[LOGFS_BLOCK_SIZE]);

void logfs_cache_put_data(
    struct logfs_cache *cache,
    uint64_t block_no,
    const uint8_t data[LOGFS_BLOCK_SIZE],
    int dirty);

void logfs_cache_invalidate_data(
    struct logfs_cache *cache,
    uint64_t block_no);

/* Inode-cache operations. */
int logfs_cache_get_inode(
    struct logfs_cache *cache,
    uint64_t ino,
    struct logfs_inode *out);

void logfs_cache_put_inode(
    struct logfs_cache *cache,
    uint64_t ino,
    const struct logfs_inode *inode,
    int dirty);

void logfs_cache_invalidate_inode(
    struct logfs_cache *cache,
    uint64_t ino);




/* ---------- Segment write buffer ---------- */

/* Initialize the buffer for a specified segment. */
int logfs_segment_buffer_init(
    struct logfs_segment_buffer *segment,
    uint64_t segment_start_block,
    uint32_t segment_seq);

/* Append one complete block.
 * block_type is the value stored in the segment header's block_map.
 * Returns the slot number, or a negative error code.
 */
int logfs_segment_buffer_append(
    struct logfs_segment_buffer *segment,
    const uint8_t data[LOGFS_BLOCK_SIZE],
    int32_t block_type);

/* Write the entire segment buffer to the image and sync it.
 * Returns 0 on success or a negative error code.
 */
int logfs_segment_buffer_flush(
    struct logfs_segment_buffer *segment,
    int fd);


/* Return 1 if the block is buffered, 0 if not found. */
int logfs_segment_buffer_get(
    const struct logfs_segment_buffer *segment,
    uint64_t block_no,
    uint8_t out[LOGFS_BLOCK_SIZE]);


#endif
