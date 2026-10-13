#define _FILE_OFFSET_BITS 64
#define _DEFAULT_SOURCE

#include "cache.h"

#include <errno.h>
#include <stddef.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>

/*
 * A segment contains 64 blocks:
 * slot 0 is the header; slots 1..63 are appendable.
 */
int logfs_segment_buffer_init(
    struct logfs_segment_buffer *segment,
    uint64_t segment_start_block,
    uint32_t segment_seq)
{
    if (segment == NULL)
        return -EINVAL;

    memset(segment, 0, sizeof(*segment));

    segment->segment_start_block = segment_start_block;
    segment->next_slot = 1;
    segment->dirty = 0;

    struct logfs_segment_hdr *header =
        (struct logfs_segment_hdr *)segment->bytes[0];

    header->magic = LOGFS_SEG_MAGIC;
    header->seg_seq = segment_seq;
    header->flags = 0;

    for (size_t i = 0; i < LOGFS_SEG_BLOCKS; i++)
        header->block_map[i] = LOGFS_BLK_FREE;

    header->crc32 = logfs_crc32(
        header, offsetof(struct logfs_segment_hdr, crc32));

    return 0;
}

/*
 * Append one complete block to the in-memory segment.
 * The caller chooses the block-map value:
 *   LOGFS_BLK_INODE for an inode,
 *   nonnegative inode number for file/directory data,
 *   LOGFS_BLK_REC_HEADER for a record header.
 *
 * Returns the assigned slot, or -EINVAL / -ENOSPC.
 */
int logfs_segment_buffer_append(
    struct logfs_segment_buffer *segment,
    const uint8_t data[LOGFS_BLOCK_SIZE],
    int32_t block_type)
{
    if (segment == NULL || data == NULL)
        return -EINVAL;

    if (segment->next_slot == 0 ||
        segment->next_slot >= LOGFS_SEG_BLOCKS)
        return -ENOSPC;

    uint32_t slot = segment->next_slot;

    memcpy(segment->bytes[slot], data, LOGFS_BLOCK_SIZE);

    struct logfs_segment_hdr *header =
        (struct logfs_segment_hdr *)segment->bytes[0];

    header->block_map[slot] = block_type;
    segment->next_slot++;
    segment->dirty = 1;

    return (int)slot;
}


int logfs_segment_buffer_get(
    const struct logfs_segment_buffer *segment,
    uint64_t block_no,
    uint8_t out[LOGFS_BLOCK_SIZE])
{
    if (segment == NULL || out == NULL)
        return 0;

    /*
     * Slot 0 contains the segment header.
     * Slots [1, next_slot) contain appended blocks.
     */
    if (block_no <= segment->segment_start_block)
        return 0;

    uint64_t slot64 = block_no - segment->segment_start_block;

    if (slot64 >= segment->next_slot ||
        slot64 >= LOGFS_SEG_BLOCKS)
        return 0;

    uint32_t slot = (uint32_t)slot64;

    memcpy(out, segment->bytes[slot], LOGFS_BLOCK_SIZE);
    return 1;
}



/*
 * Flush a complete segment to disk.
 *
 * This function writes all 64 blocks, including unused zero-filled
 * slots, and calls fsync() after a successful write.
 */
int logfs_segment_buffer_flush(
    struct logfs_segment_buffer *segment,
    int fd)
{
    if (segment == NULL || fd < 0)
        return -EINVAL;

    if (!segment->dirty)
        return 0;

    struct logfs_segment_hdr *header =
        (struct logfs_segment_hdr *)segment->bytes[0];

    header->crc32 = logfs_crc32(
        header, offsetof(struct logfs_segment_hdr, crc32));

    size_t total_bytes =
        (size_t)LOGFS_SEG_BLOCKS * LOGFS_BLOCK_SIZE;

    const uint8_t *bytes = &segment->bytes[0][0];
    size_t written = 0;
    off_t offset = (off_t)(
        segment->segment_start_block * LOGFS_BLOCK_SIZE);

    while (written < total_bytes) {
        ssize_t n = pwrite(
            fd,
            bytes + written,
            total_bytes - written,
            offset + (off_t)written);

        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -errno;
        }

        if (n == 0)
            return -EIO;

        written += (size_t)n;
    }

    if (fsync(fd) < 0)
        return -errno;

    segment->dirty = 0;
    return 0;
}
