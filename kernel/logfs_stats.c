// SPDX-License-Identifier: GPL-2.0
/*
 * logfs_stats.c - Flush statistics kernel module for LogFS (Task F)
 *
 * Exposes flush statistics via sysfs:
 *   /sys/kernel/logfs_stats/flush_count (read-only counter)
 *   /sys/kernel/logfs_stats/notify      (write '1' to increment flush_count)
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include <linux/string.h>
#include <linux/atomic.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("LogFS Team");
MODULE_DESCRIPTION("Log-Structured Filesystem Flush Statistics Module");
MODULE_VERSION("1.0");

/* Atomic counter tracking total number of successful fsync/flushes */
static atomic_t flush_count = ATOMIC_INIT(0);

/* Sysfs kobject reference */
static struct kobject *logfs_kobj;

/*
 * Reading flush_count: returns an unsigned integer followed by a newline.
 */
static ssize_t flush_count_show(struct kobject *kobj,
                                struct kobj_attribute *attr,
                                char *buf)
{
    (void)kobj;
    (void)attr;
    return sysfs_emit(buf, "%u\n", (unsigned int)atomic_read(&flush_count));
}

/*
 * Writing to notify:
 * If the input starts with '1' or parses as unsigned integer 1,
 * increment flush_count by one. Other inputs are ignored.
 */
static ssize_t notify_store(struct kobject *kobj,
                            struct kobj_attribute *attr,
                            const char *buf,
                            size_t count)
{
    unsigned int val;
    int err;

    (void)kobj;
    (void)attr;

    /*
     * Parse input as integer or check leading character '1'.
     * Handles both "1\n" from echo/write and plain "1".
     */
    err = kstrtouint(buf, 10, &val);
    if ((err == 0 && val == 1) || (count > 0 && buf[0] == '1')) {
        atomic_inc(&flush_count);
    }

    return count;
}

/*
 * Attribute definitions:
 * flush_count: mode 0444 (read-only by everyone)
 * notify:      mode 0666 (writable by userspace processes running LFS)
 */
static struct kobj_attribute flush_count_attribute =
    __ATTR(flush_count, 0444, flush_count_show, NULL);

static struct kobj_attribute notify_attribute =
    __ATTR(notify, 0666, NULL, notify_store);

static struct attribute *logfs_stats_attrs[] = {
    &flush_count_attribute.attr,
    &notify_attribute.attr,
    NULL,
};

static const struct attribute_group logfs_stats_group = {
    .attrs = logfs_stats_attrs,
};

static int __init logfs_stats_init(void)
{
    int ret;

    /* Create /sys/kernel/logfs_stats */
    logfs_kobj = kobject_create_and_add("logfs_stats", kernel_kobj);
    if (!logfs_kobj) {
        pr_err("logfs_stats: failed to create kobject under /sys/kernel\n");
        return -ENOMEM;
    }

    /* Create sysfs attribute files under the kobject */
    ret = sysfs_create_group(logfs_kobj, &logfs_stats_group);
    if (ret) {
        pr_err("logfs_stats: failed to create sysfs attributes (error %d)\n", ret);
        kobject_put(logfs_kobj);
        return ret;
    }

    atomic_set(&flush_count, 0);
    pr_info("logfs_stats: loaded successfully; /sys/kernel/logfs_stats ready\n");
    return 0;
}

static void __exit logfs_stats_exit(void)
{
    /* Cleanly remove sysfs entries and kobject */
    if (logfs_kobj) {
        sysfs_remove_group(logfs_kobj, &logfs_stats_group);
        kobject_put(logfs_kobj);
        logfs_kobj = NULL;
    }
    pr_info("logfs_stats: unloaded cleanly\n");
}

module_init(logfs_stats_init);
module_exit(logfs_stats_exit);
