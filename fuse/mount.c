#define FUSE_USE_VERSION 31
#define _FILE_OFFSET_BITS 64
#define _DEFAULT_SOURCE

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/stat.h>

#include "lfs.h"

/*
 * Configuration options for mount.logfs
 */
struct logfs_options {
    const char *img_path;
    int show_help;
};

static struct logfs_options options = {
    .img_path = "logfs.img",
    .show_help = 0,
};

#define LOGFS_OPT(t, p) { t, offsetof(struct logfs_options, p), 1 }

static const struct fuse_opt option_spec[] = {
    LOGFS_OPT("--img=%s", img_path),
    LOGFS_OPT("--image=%s", img_path),
    LOGFS_OPT("-h", show_help),
    LOGFS_OPT("--help", show_help),
    FUSE_OPT_END
};

/*
 * Task E.2 Lifecycle Callbacks
 */

static void *logfs_fuse_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
    (void)conn;
    if (cfg) {
        /*
         * Disable kernel caching so directory updates and file reads go
         * straight to the LFS library and segment write buffer, ensuring
         * consistency across operations and remounts.
         */
        cfg->kernel_cache = 0;
    }

    const char *img = options.img_path ? options.img_path : "logfs.img";
    int ret = lfs_mount(img);
    if (ret != 0) {
        fprintf(stderr, "mount.logfs: lfs_mount(\"%s\") failed with error %d (%s)\n",
                img, ret, strerror(-ret));
    }
    return NULL;
}

static void logfs_fuse_destroy(void *private_data)
{
    (void)private_data;
    int ret = lfs_umount();
    if (ret != 0) {
        fprintf(stderr, "mount.logfs: lfs_umount() failed with error %d (%s)\n",
                ret, strerror(-ret));
    }
}

/*
 * Task E.3 Callbacks (thin wrappers around fixed lfs_* prototypes)
 */

static int logfs_fuse_getattr(const char *path, struct stat *stbuf,
                              struct fuse_file_info *fi)
{
    (void)fi;
    memset(stbuf, 0, sizeof(struct stat));
    return lfs_getattr(path, stbuf);
}

/*
 * Adapter context and helper to bridge lfs_dir_filler_t to fuse_fill_dir_t
 */
struct dir_filler_wrapper {
    void *buf;
    fuse_fill_dir_t filler;
};

static int dir_filler_adapter(void *buf, const char *name, uint64_t ino, mode_t mode)
{
    struct dir_filler_wrapper *wrap = (struct dir_filler_wrapper *)buf;
    struct stat st;
    memset(&st, 0, sizeof(st));
    st.st_ino = (ino_t)ino;
    st.st_mode = mode;
    return wrap->filler(wrap->buf, name, &st, 0, 0);
}

static int logfs_fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                              off_t offset, struct fuse_file_info *fi,
                              enum fuse_readdir_flags flags)
{
    (void)offset;
    (void)fi;
    (void)flags;
    struct dir_filler_wrapper wrap = {
        .buf = buf,
        .filler = filler
    };
    return lfs_readdir(path, &wrap, dir_filler_adapter);
}

static int logfs_fuse_open(const char *path, struct fuse_file_info *fi)
{
    (void)fi;
    uint64_t ino = 0;
    return lfs_lookup(path, &ino);
}

static int logfs_fuse_read(const char *path, char *buf, size_t size, off_t offset,
                           struct fuse_file_info *fi)
{
    (void)fi;
    return lfs_read(path, buf, size, offset);
}

static int logfs_fuse_write(const char *path, const char *buf, size_t size, off_t offset,
                            struct fuse_file_info *fi)
{
    (void)fi;
    return lfs_write(path, buf, size, offset);
}

static int logfs_fuse_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    (void)fi;
    return lfs_create(path, mode);
}

static int logfs_fuse_mkdir(const char *path, mode_t mode)
{
    return lfs_mkdir(path, mode);
}

static int logfs_fuse_rmdir(const char *path)
{
    return lfs_rmdir(path);
}

static int logfs_fuse_unlink(const char *path)
{
    return lfs_unlink(path);
}

static int logfs_fuse_opendir(const char *path, struct fuse_file_info *fi)
{
    (void)fi;
    struct stat st;
    int ret = lfs_getattr(path, &st);
    if (ret != 0) {
        return ret;
    }
    if (!S_ISDIR(st.st_mode)) {
        return -ENOTDIR;
    }
    return 0;
}

static int logfs_fuse_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
    (void)fi;
    return lfs_truncate(path, size);
}

static int logfs_fuse_utimens(const char *path, const struct timespec tv[2],
                              struct fuse_file_info *fi)
{
    (void)path;
    (void)tv;
    (void)fi;
    return 0;
}

static const struct fuse_operations logfs_oper = {
    .init       = logfs_fuse_init,
    .destroy    = logfs_fuse_destroy,
    .getattr    = logfs_fuse_getattr,
    .readdir    = logfs_fuse_readdir,
    .open       = logfs_fuse_open,
    .read       = logfs_fuse_read,
    .write      = logfs_fuse_write,
    .create     = logfs_fuse_create,
    .mkdir      = logfs_fuse_mkdir,
    .rmdir      = logfs_fuse_rmdir,
    .unlink     = logfs_fuse_unlink,
    .opendir    = logfs_fuse_opendir,
    .truncate   = logfs_fuse_truncate,
    .utimens    = logfs_fuse_utimens,
};

static void show_help(const char *progname)
{
    printf("Usage: %s [options] <mountpoint>\n\n", progname);
    printf("Mounts a log-structured filesystem (LFS) image via FUSE3 (Task E).\n\n");
    printf("Options:\n");
    printf("    --img=<path>       Backing disk image (default: logfs.img)\n");
    printf("    --image=<path>     Alias for --img\n");
    printf("    -f                 Run in foreground (do not daemonize)\n");
    printf("    -d                 Run in debug mode (enables verbose logs)\n");
    printf("    -s                 Run single-threaded\n");
    printf("    -h, --help         Print this help message\n\n");
    printf("Unmount:\n");
    printf("    fusermount3 -u <mountpoint>\n\n");
}

int main(int argc, char *argv[])
{
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);

    options.img_path = "logfs.img";
    options.show_help = 0;

    if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1) {
        return 1;
    }

    if (options.show_help) {
        show_help(argv[0]);
        fuse_opt_add_arg(&args, "--help");
        args.argv[0][0] = '\0';
    }

    /*
     * Support positional syntax:
     * e.g., ./mount.logfs logfs.img /mnt/logfs
     * If two non-option arguments are passed and the first is an existing regular file,
     * treat it as the image path and the second as the mountpoint.
     */
    int nonopt_count = 0;
    int first_nonopt = -1;
    for (int i = 1; i < args.argc; i++) {
        if (args.argv[i][0] != '-') {
            nonopt_count++;
            if (nonopt_count == 1) {
                first_nonopt = i;
            }
        }
    }
    if (nonopt_count == 2 && first_nonopt > 0) {
        struct stat st;
        if (stat(args.argv[first_nonopt], &st) == 0 && S_ISREG(st.st_mode)) {
            options.img_path = args.argv[first_nonopt];
            /* Remove the image argument from args so only the mountpoint remains */
            for (int j = first_nonopt; j < args.argc - 1; j++) {
                args.argv[j] = args.argv[j + 1];
            }
            args.argc--;
        }
    }

    return fuse_main(args.argc, args.argv, &logfs_oper, NULL);
}
