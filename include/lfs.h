#ifndef LFS_H
#define LFS_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Task C: Library Operations (lfs_*)
 * ============================================================================
 * Return 0 on success, or a negative errno on failure (e.g. -ENOENT, -EEXIST,
 * -ENOTEMPTY, -ENOSPC). For lfs_read / lfs_write, return byte count on success
 * (or negative errno). Paths are absolute within the filesystem (e.g. "/",
 * "/docs", "/docs/a.txt").
 */

/* C.1 Updates */
int lfs_create(const char *path, mode_t mode);
int lfs_mkdir(const char *path, mode_t mode);
int lfs_write(const char *path, const char *buf, size_t size, off_t offset);
int lfs_rmdir(const char *path);

/* C.2 Reads */
int lfs_lookup(const char *path, uint64_t *ino_out);
int lfs_getattr(const char *path, struct stat *stbuf);

typedef int (*lfs_dir_filler_t)(void *buf, const char *name,
                                uint64_t ino, mode_t mode);
int lfs_readdir(const char *path, void *buf, lfs_dir_filler_t filler);
int lfs_read(const char *path, char *buf, size_t size, off_t offset);

/* C.3 Mount and Lifecycle */
int lfs_mount(const char *img_path);
int lfs_umount(void);

/* Optional / Extended Operations for FUSE */
int lfs_unlink(const char *path);
int lfs_truncate(const char *path, off_t size);

#ifdef __cplusplus
}
#endif

#endif /* LFS_H */
