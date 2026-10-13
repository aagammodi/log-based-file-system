# Log-Structured Filesystem (LFS) with FUSE
**Course:** Advances in OS Design (Autumn 2026–2027)  
**Assignment 3:** Log-Structured Filesystem with FUSE  

---

## 1. Team Information
- **Group Number:** `<group_number>`
- **Team Members:**
  - Arun Balaji
  - Aagam Modi

---

## 2. Overview and Architecture

This project implements a simplified log-structured filesystem (LFS) in user space exposing POSIX filesystem operations via FUSE (Filesystem in Userspace) and tracking disk flush statistics using a custom Linux kernel module.

### Directory Layout
- **`include/`**: Header files defining disk layout, caching, and LFS APIs:
  - `logfs.h`: On-disk structures (superblock, checkpoint, segment header, inodes, dirents, log records).
  - `cache.h`: In-memory cache structures and segment write buffer interfaces.
  - `lfs.h`: Public library prototypes (`lfs_*`) for Task C and Task E.
- **`src/`**: Filesystem core library and formatting utility:
  - `mkfs.c`: Image formatter (`mkfs.logfs`).
  - `disk.c`: Block-level I/O routines (`pread`/`pwrite`/`fsync`).
  - `crc32.c`: Reflected CRC32 checksum routines.
  - `cache.c`: Inode and data block caches.
  - `segment_buffer.c`: 256 KiB in-memory segment write buffer.
- **`fuse/`**: FUSE integration (Task E):
  - `mount.c`: Thin wrapper program `mount.logfs` using `libfuse3`.
  - `lfs.h`: Proxy header inclusion.
- **`kernel/`**: Flush statistics loadable kernel module (Task F):
  - `logfs_stats.c`: Kernel module source exposing sysfs entries.
  - `Makefile`: Kbuild configuration.
- **`tests/`**: Unit tests and verification drivers.

---

## 3. Build Instructions

### Prerequisites
- Linux kernel with FUSE support.
- `libfuse3-dev`, `fuse3`, `pkg-config`, `gcc`, and `make`.
- Linux kernel headers for building the kernel module (`linux-headers-$(uname -r)`).

```bash
# Ubuntu / Debian
sudo apt-get update && sudo apt-get install -y libfuse3-dev fuse3 pkg-config build-essential linux-headers-$(uname -r)
```

### Building the Project
From the project root:

```bash
# Build the disk formatter (mkfs.logfs)
make mkfs.logfs

# Build the FUSE daemon (mount.logfs)
make mount.logfs

# Build the kernel module (logfs_stats.ko)
make logfs_stats

# Clean all build artifacts
make clean
```

---

## 4. Disk Layout & Data Structures (Task A)

- **Backing File:** Single 256 MiB image file named `logfs.img` ($65,536$ blocks of $4,096$ bytes each).
- **Block Layout:**
  - **Block 0 (Superblock):** Fixed filesystem metadata, magic `0x4C4F4746`, pointers to checkpoint and log segment start block.
  - **Blocks 1–3 (Checkpoint):** Durable allocation state: full inode map (`imap`), `inode_bitmap`, `segment_bitmap`, and CRC32.
  - **Blocks 4–65535 (Log Segments):** 1023 segments of 64 blocks ($256\text{ KiB}$) each.
- **Rules:**
  - Copy-on-Write (COW): data and inode blocks are never overwritten in place in the log.
  - Checkpoint region is updated in-place during write-back to track latest imap tips.
  - CRC32 verification ensures data integrity on mount.

---

## 5. Cache Design & Segment Write Buffer (Task B)

### Required Caches
1. **Data Block Cache:** 64 entries. Stores cached data blocks indexed by block number with valid, dirty, and last-used timestamp flags.
2. **Inode Cache:** 64 entries. Stores parsed `struct logfs_inode` indexed by inode number.
3. **Checkpoint Cache:** Single in-memory copy of `struct logfs_checkpoint` tracking full imap and bitmaps.
4. **Superblock Cache:** Single in-memory copy of `struct logfs_super`.

### Segment Write Buffer
- Exactly one in-memory buffer of 64 blocks ($256\text{ KiB}$) representing the currently active open segment.
- Block 0 stores the segment header (`struct logfs_segment_hdr`) with its block allocation map (`block_map`).
- New data blocks, inode blocks, and log record headers (`struct logfs_rec_hdr`) are sequentially appended here.

### Invalidation and Eviction Policies
- **Read Lookup Order:**
  1. Check data / inode cache.
  2. Check current open segment write buffer for unflushed updates.
  3. On cache miss, issue `pread` from `logfs.img` and insert into cache.
- **Invalidation:** On every COW update or deletion, existing cache entries for old block numbers or modified inodes are invalidated.
- **Eviction:** When a cache reaches 64 entries, a **Least Recently Used (LRU)** policy evicts the entry with the oldest access timestamp.

### Flush & Write-back Policy
- **Segment Flush:** When the segment write buffer becomes full (or on unmount), the segment header's `block_map` and CRC32 are finalized, the segment is sealed (`LOGFS_SEG_SEALED`), and all 64 blocks are flushed to disk using `logfs_write_block` followed by `fsync`.
- **Checkpoint Write-back:** The checkpoint (imap and allocation bitmaps) and superblock are written to disk upon segment flush and during `lfs_umount`.

---

## 6. Library Operations (Task C)

Exposes the standard `lfs_*` prototypes in `include/lfs.h`:
- `lfs_create`, `lfs_mkdir`, `lfs_write`, `lfs_rmdir`, `lfs_unlink`, `lfs_truncate`
- `lfs_lookup`, `lfs_getattr`, `lfs_readdir`, `lfs_read`
- `lfs_mount`, `lfs_umount`
- All functions return `0` on success (or byte count for `read`/`write`) and standard negative error codes (`-ENOENT`, `-EEXIST`, `-ENOTEMPTY`, `-ENOSPC`, `-EISDIR`, `-ENOTDIR`) on failure.

---

## 7. Garbage Collection Policy (Task D)

- **When GC Runs:** Triggered when segment allocation finds no available free segments in `segment_bitmap` (or log utilization exceeds the high watermark).
- **Candidate Selection:** Only sealed segments are eligible. The cleaner calculates the **live ratio** of each sealed segment:
  $$\text{live ratio} = \frac{\#\{\text{live mapped blocks}\}}{\#\{\text{total non-free mapped blocks}\}}$$
  The cleaner selects the segment with the lowest live ratio, strongly prioritizing segments with live ratio $< 25\%$.
- **Finding Live Blocks:**
  - For inode blocks (`LOGFS_BLK_INODE` / `-2`): Checked against `cp.imap[ino]`. If the current imap points to this block, it is live.
  - For data blocks (`block_map[i] = ino`): Checked against `direct[]` pointers of that inode. If any `direct[k]` still points to this block, it is live.
- **Cleaning:** Live blocks are copied forward into the active segment write buffer as new COW records, inode pointers/imap entries are updated, and the cleaned segment is marked free in `segment_bitmap`.

---

## 8. FUSE Integration — `mount.logfs` (Task E)

`mount.logfs` exposes the filesystem via libfuse3. It acts strictly as a **thin wrapper** over the `lfs_*` API without containing filesystem logic.

### Mounting and Unmounting
```bash
# 1. Format disk image
./mkfs.logfs

# 2. Create mount point
mkdir -p /mnt/logfs

# 3. Mount in foreground (recommended for testing / debugging)
./mount.logfs -f /mnt/logfs

# Or mount in background
./mount.logfs /mnt/logfs

# Unmount cleanly
fusermount3 -u /mnt/logfs
```

### Supported Arguments
- `[mountpoint]`: Directory where the filesystem is mounted.
- `--img=<path>`: Path to backing disk image (defaults to `logfs.img`).
- `-f`: Foreground mode.
- `-d`: Debug mode (verbose FUSE debug logs).
- `-s`: Single-threaded mode.

### Sample Workflow Verification (Task E.5)
```bash
./mkfs.logfs
mkdir -p /mnt/logfs
./mount.logfs -f /mnt/logfs &
ls /mnt/logfs
mkdir /mnt/logfs/docs
echo hello > /mnt/logfs/docs/a.txt
cat /mnt/logfs/docs/a.txt
fusermount3 -u /mnt/logfs
./mount.logfs -f /mnt/logfs &
cat /mnt/logfs/docs/a.txt
fusermount3 -u /mnt/logfs
```

---

## 9. Flush Statistics Kernel Module — `logfs_stats` (Task F)

A loadable Linux kernel module exposing flush statistics via sysfs.

### Sysfs Attributes
- `/sys/kernel/logfs_stats/flush_count` (mode `0444`, read-only): Returns the current number of completed flushes as an unsigned integer and newline.
- `/sys/kernel/logfs_stats/notify` (mode `0666`, writable): Writing `1` increments `flush_count` by one. Non-`1` writes are ignored.

### How the Library Uses `notify`
After every successful `fsync` call on `logfs.img` (during segment buffer flush or `lfs_umount`), the LFS library writes `1` to `/sys/kernel/logfs_stats/notify`:

```c
static void notify_flush(void)
{
    int fd = open("/sys/kernel/logfs_stats/notify", O_WRONLY);
    if (fd >= 0) {
        ssize_t w = write(fd, "1\n", 2);
        (void)w;
        close(fd);
    }
}
```
*If `logfs_stats.ko` is not loaded, `open` returns `-1` and the library gracefully skips the write without crashing.*

### Kernel Module Usage
```bash
# Build module
make logfs_stats

# Load module
sudo insmod kernel/logfs_stats.ko

# Check counter
cat /sys/kernel/logfs_stats/flush_count
# Output: 0

# Trigger updates that cause fsync ...
cat /sys/kernel/logfs_stats/flush_count
# Counter increases

# Unload module
sudo rmmod logfs_stats
```