
#include "cache.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition, message)                 \
    do {                                         \
        if (!(condition)) {                      \
            fprintf(stderr, "FAIL: %s\n", message); \
            return 1;                            \
        }                                        \
    } while (0)

int main(void)
{
    struct logfs_cache cache;
    uint8_t input[LOGFS_BLOCK_SIZE];
    uint8_t output[LOGFS_BLOCK_SIZE];
    struct logfs_inode inode;
    struct logfs_inode got_inode;

    logfs_cache_init(&cache);

    /* Test 1: a new cache must miss. */
    CHECK(!logfs_cache_get_data(&cache, 100, output),
          "empty data cache should miss");
    puts("PASS: data cache miss");

    /* Test 2: insert and retrieve a data block. */
    memset(input, 0x5A, sizeof(input));
    logfs_cache_put_data(&cache, 100, input, 0);

    CHECK(logfs_cache_get_data(&cache, 100, output),
          "inserted data block should hit");
    CHECK(memcmp(input, output, sizeof(input)) == 0,
          "retrieved data must match inserted data");
    puts("PASS: data cache hit");

    /* Test 3: invalidate the data block. */
    logfs_cache_invalidate_data(&cache, 100);
    CHECK(!logfs_cache_get_data(&cache, 100, output),
          "invalidated data block should miss");
    puts("PASS: data cache invalidation");

    /* Test 4: inode cache insertion and lookup. */
    memset(&inode, 0, sizeof(inode));
    inode.ino = 7;
    inode.mode = 0100644;
    inode.size = 123;

    logfs_cache_put_inode(&cache, inode.ino, &inode, 0);

    CHECK(logfs_cache_get_inode(&cache, 7, &got_inode),
          "inserted inode should hit");
    CHECK(got_inode.ino == 7 && got_inode.size == 123,
          "retrieved inode fields must match");
    puts("PASS: inode cache hit");

    /* Test 5: inode invalidation. */
    logfs_cache_invalidate_inode(&cache, 7);
    CHECK(!logfs_cache_get_inode(&cache, 7, &got_inode),
          "invalidated inode should miss");
    puts("PASS: inode cache invalidation");

    /*
     * Test 6: LRU eviction in the data cache.
     *
     * Fill all 64 entries. Access block 1000 again so it becomes
     * recently used. Insert one more block; block 1001 should be
     * evicted because it is now the least recently used.
     */
    logfs_cache_init(&cache);
    memset(input, 0xA5, sizeof(input));

    for (uint64_t i = 0; i < LOGFS_DATA_CACHE_ENTRIES; i++)
        logfs_cache_put_data(&cache, 1000 + i, input, 0);

    CHECK(logfs_cache_get_data(&cache, 1000, output),
          "first block should be present before eviction");

    logfs_cache_put_data(&cache, 2000, input, 0);

    CHECK(logfs_cache_get_data(&cache, 1000, output),
          "recently used block should survive eviction");
    CHECK(!logfs_cache_get_data(&cache, 1001, output),
          "least recently used block should be evicted");
    CHECK(logfs_cache_get_data(&cache, 2000, output),
          "new block should be present after insertion");
    puts("PASS: data cache LRU eviction");

    puts("All cache tests passed.");
    return 0;
}
