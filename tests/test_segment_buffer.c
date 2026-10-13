
#define _FILE_OFFSET_BITS 64
#define _DEFAULT_SOURCE

#include "cache.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition, message)                  \
    do {                                           \
        if (!(condition)) {                       \
            fprintf(stderr, "FAIL: %s\n", message); \
            return 1;                              \
        }                                          \
    } while (0)


int main(void)
{
    struct logfs_segment_buffer segment;
    uint8_t block[LOGFS_BLOCK_SIZE];
    uint8_t readback[LOGFS_BLOCK_SIZE];

    /* Test 1: Initialize an in-memory segment. */
    CHECK(logfs_segment_buffer_init(&segment, 68, 1) == 0,
          "segment initialization failed");

    struct logfs_segment_hdr *header =
        (struct logfs_segment_hdr *)segment.bytes[0];

    CHECK(header->magic == LOGFS_SEG_MAGIC,
          "incorrect segment magic");
    CHECK(header->seg_seq == 1,
          "incorrect segment sequence");
    CHECK(segment.next_slot == 1,
          "append position must start at slot 1");
    CHECK(!segment.dirty,
          "new segment should not be dirty");

    for (size_t i = 0; i < LOGFS_SEG_BLOCKS; i++) {
        CHECK(header->block_map[i] == LOGFS_BLK_FREE,
              "block map should initially be free");
    }

    puts("PASS: segment initialization");

    /* Test 2: Append one data block. */
    memset(block, 0x5A, sizeof(block));

    int slot = logfs_segment_buffer_append(
        &segment, block, 0);

    CHECK(slot == 1, "first block must occupy slot 1");
    CHECK(segment.next_slot == 2,
          "append position should advance");
    CHECK(segment.dirty,
          "appending should mark segment dirty");
    CHECK(header->block_map[1] == 0,
          "block map should record inode number");
    CHECK(memcmp(segment.bytes[1], block, sizeof(block)) == 0,
          "appended data should match");

    puts("PASS: block append and block-map update");

    /* Test 3: Read a block from the buffer before flushing. */
    memset(readback, 0, sizeof(readback));

    CHECK(logfs_segment_buffer_get(&segment, 69, readback),
          "buffered block should be found");

    CHECK(memcmp(readback, segment.bytes[1],
                 LOGFS_BLOCK_SIZE) == 0,
          "buffered lookup returned incorrect data");

    CHECK(!logfs_segment_buffer_get(&segment, 68, readback),
          "segment header must not be returned as a body block");

    CHECK(!logfs_segment_buffer_get(&segment, 70, readback),
          "unappended block should not be found");

    puts("PASS: buffered block lookup");

    /* Test 4: Fill the remaining body slots. */
    memset(block, 0xA5, sizeof(block));

    for (uint32_t expected_slot = 2;
         expected_slot < LOGFS_SEG_BLOCKS;
         expected_slot++) {
        slot = logfs_segment_buffer_append(
            &segment, block, LOGFS_BLK_INODE);

        CHECK(slot == (int)expected_slot,
              "unexpected append slot");
    }

    CHECK(segment.next_slot == LOGFS_SEG_BLOCKS,
          "full segment should have next_slot equal to 64");

    CHECK(logfs_segment_buffer_append(
              &segment, block, LOGFS_BLK_INODE) == -ENOSPC,
          "append to full segment should return ENOSPC");

    puts("PASS: full segment and ENOSPC");

    /*
     * Test 5: Flush the segment to disk.
     * Segment 1 starts at block 68 (4 + 64).
     * The formatter recreates the image before this test runs.
     */
    int fd = open("logfs.img", O_RDWR);
    CHECK(fd >= 0, "could not open logfs.img");

    CHECK(logfs_segment_buffer_flush(&segment, fd) == 0,
          "segment flush failed");
    CHECK(!segment.dirty,
          "successful flush should clear dirty flag");

    puts("PASS: segment flush");

    /* Test 6: Verify the flushed header and body. */
    CHECK(logfs_read_block(fd, 68, readback) == 0,
          "could not read flushed segment header");

    struct logfs_segment_hdr disk_header;
    memcpy(&disk_header, readback, sizeof(disk_header));

    CHECK(disk_header.magic == LOGFS_SEG_MAGIC,
          "flushed header magic is incorrect");
    CHECK(disk_header.seg_seq == 1,
          "flushed header sequence is incorrect");

    CHECK(logfs_crc32(
              &disk_header,
              offsetof(struct logfs_segment_hdr, crc32))
              == disk_header.crc32,
          "flushed header CRC is incorrect");

    CHECK(logfs_read_block(fd, 69, readback) == 0,
          "could not read first flushed body block");

    CHECK(memcmp(readback, segment.bytes[1],
                 LOGFS_BLOCK_SIZE) == 0,
          "flushed body data differs");

    close(fd);

    puts("PASS: flushed segment readback and CRC");
    puts("All segment-buffer tests passed.");

    return 0;
}

