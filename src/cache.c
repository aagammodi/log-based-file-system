
#include "cache.h"

#include <string.h>

/*
 * Initialize all cache entries as invalid.
 * The checkpoint, superblock, and segment buffer are zeroed.
 */
void logfs_cache_init(struct logfs_cache *cache)
{
    if (cache == NULL)
        return;

    memset(cache, 0, sizeof(*cache));
}

/* ---------- Data-block cache ---------- */

int logfs_cache_get_data(
    struct logfs_cache *cache,
    uint64_t block_no,
    uint8_t out[LOGFS_BLOCK_SIZE])
{
    if (cache == NULL || out == NULL)
        return 0;

    for (size_t i = 0; i < LOGFS_DATA_CACHE_ENTRIES; i++) {
        struct logfs_data_cache_entry *entry = &cache->data[i];

        if (entry->valid && entry->block_no == block_no) {
            memcpy(out, entry->data, LOGFS_BLOCK_SIZE);
            entry->last_used = ++cache->access_clock;
            return 1;
        }
    }

    return 0;
}

void logfs_cache_put_data(
    struct logfs_cache *cache,
    uint64_t block_no,
    const uint8_t data[LOGFS_BLOCK_SIZE],
    int dirty)
{
    if (cache == NULL || data == NULL)
        return;

    struct logfs_data_cache_entry *target = NULL;

    /* Prefer an existing entry for this block. */
    for (size_t i = 0; i < LOGFS_DATA_CACHE_ENTRIES; i++) {
        struct logfs_data_cache_entry *entry = &cache->data[i];

        if (entry->valid && entry->block_no == block_no) {
            target = entry;
            break;
        }
    }

    /* Otherwise prefer an unused entry. */
    if (target == NULL) {
        for (size_t i = 0; i < LOGFS_DATA_CACHE_ENTRIES; i++) {
            if (!cache->data[i].valid) {
                target = &cache->data[i];
                break;
            }
        }
    }

    /*
     * Cache full: evict the least recently used entry.
     *
     * Dirty entries are not written back here because log data must
     * follow the filesystem's copy-on-write and segment-flush policy.
     */
    if (target == NULL) {
        target = &cache->data[0];

        for (size_t i = 1; i < LOGFS_DATA_CACHE_ENTRIES; i++) {
            if (cache->data[i].last_used < target->last_used)
                target = &cache->data[i];
        }
    }

    target->block_no = block_no;
    memcpy(target->data, data, LOGFS_BLOCK_SIZE);
    target->last_used = ++cache->access_clock;
    target->valid = 1;
    target->dirty = !!dirty;
}

void logfs_cache_invalidate_data(
    struct logfs_cache *cache,
    uint64_t block_no)
{
    if (cache == NULL)
        return;

    for (size_t i = 0; i < LOGFS_DATA_CACHE_ENTRIES; i++) {
        struct logfs_data_cache_entry *entry = &cache->data[i];

        if (entry->valid && entry->block_no == block_no)
            entry->valid = 0;
    }
}

/* ---------- Inode cache ---------- */

int logfs_cache_get_inode(
    struct logfs_cache *cache,
    uint64_t ino,
    struct logfs_inode *out)
{
    if (cache == NULL || out == NULL)
        return 0;

    for (size_t i = 0; i < LOGFS_INODE_CACHE_ENTRIES; i++) {
        struct logfs_inode_cache_entry *entry = &cache->inodes[i];

        if (entry->valid && entry->ino == ino) {
            *out = entry->inode;
            entry->last_used = ++cache->access_clock;
            return 1;
        }
    }

    return 0;
}

void logfs_cache_put_inode(
    struct logfs_cache *cache,
    uint64_t ino,
    const struct logfs_inode *inode,
    int dirty)
{
    if (cache == NULL || inode == NULL)
        return;

    struct logfs_inode_cache_entry *target = NULL;

    /* Prefer an existing entry for this inode. */
    for (size_t i = 0; i < LOGFS_INODE_CACHE_ENTRIES; i++) {
        struct logfs_inode_cache_entry *entry = &cache->inodes[i];

        if (entry->valid && entry->ino == ino) {
            target = entry;
            break;
        }
    }

    /* Otherwise use an unused entry. */
    if (target == NULL) {
        for (size_t i = 0; i < LOGFS_INODE_CACHE_ENTRIES; i++) {
            if (!cache->inodes[i].valid) {
                target = &cache->inodes[i];
                break;
            }
        }
    }

    /* Cache full: evict the least recently used entry. */
    if (target == NULL) {
        target = &cache->inodes[0];

        for (size_t i = 1; i < LOGFS_INODE_CACHE_ENTRIES; i++) {
            if (cache->inodes[i].last_used < target->last_used)
                target = &cache->inodes[i];
        }
    }

    target->ino = ino;
    target->inode = *inode;
    target->last_used = ++cache->access_clock;
    target->valid = 1;
    target->dirty = !!dirty;
}

void logfs_cache_invalidate_inode(
    struct logfs_cache *cache,
    uint64_t ino)
{
    if (cache == NULL)
        return;

    for (size_t i = 0; i < LOGFS_INODE_CACHE_ENTRIES; i++) {
        struct logfs_inode_cache_entry *entry = &cache->inodes[i];

        if (entry->valid && entry->ino == ino)
            entry->valid = 0;
    }
}
