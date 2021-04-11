#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include "sort_impl.h"

#define DEVICE_NAME "xoroshiro128p"
#define CLASS_NAME "xoro"

/* sort */
#define SORT_NAME ksort
#define SORT_TYPE uint64_t
#define MIN(x, y) (((x) < (y) ? (x) : (y)))
#define SORT_CSWAP(x, y)                           \
    {                                              \
        SORT_TYPE _sort_swap_temp = MAX((x), (y)); \
        (x) = MIN((x), (y));                       \
        (y) = _sort_swap_temp;                     \
    }
#include "sort.h"

/*
   We now have the following functions defined
   * ksort_shell_sort
   * ksort_binary_insertion_sort
   * ksort_heap_sort
   * ksort_quick_sort
   * ksort_merge_sort
   * ksort_selection_sort
   * ksort_tim_sort

   Each takes two arguments: uint64_t *array, size_t size
*/

MODULE_LICENSE("GPL");
MODULE_AUTHOR("National Cheng Kung University, Taiwan");
MODULE_DESCRIPTION("sorting implementation");
MODULE_VERSION("0.1");

extern void seed(uint64_t, uint64_t);
extern void jump(void);
extern uint64_t next(void);

static int major_number;
static struct class *dev_class = NULL;
static struct device *dev_device = NULL;

/* Count the number of times device is opened */
static int n_opens = 0;

/* Mutex to allow only one userspace program to read at once */
static DEFINE_MUTEX(xoroshiro128p_mutex);

/**
 * Devices are represented as file structure in the kernel.
 */
static int dev_open(struct inode *, struct file *);
static int dev_release(struct inode *, struct file *);
static ssize_t dev_read(struct file *, char *, size_t, loff_t *);
static ssize_t dev_write(struct file *, const char *, size_t size, loff_t *);
static struct file_operations fops = {
    .open = dev_open,
    .read = dev_read,
    .release = dev_release,
    .write = dev_write,
};

#define TEST_LEN 1000
//#define TEST_LEN 2000

static int __init cmpint(const void *a, const void *b)
{
    return *(int *) a - *(int *) b;
}


static int __init cmpuint64(const void *a, const void *b)
{
    uint64_t a_val = *(uint64_t *) a;
    uint64_t b_val = *(uint64_t *) b;
    if (a_val > b_val)
        return 1;
    if (a_val == b_val)
        return 0;
    return -1;
}

/** @brief Initialize /dev/xoroshiro128p.
 *  @return Returns 0 if successful.
 */
static int __init xoro_init(void)
{
    int *a, i, r = 1, err = -ENOMEM;
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (0 > major_number) {
        printk(KERN_ALERT "XORO: Failed to register major_number\n");
        return major_number;
    }

    dev_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(dev_class)) {
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "XORO: Failed to create dev_class\n");
        return PTR_ERR(dev_class);
    }

    dev_device = device_create(dev_class, NULL, MKDEV(major_number, 0), NULL,
                               DEVICE_NAME);
    if (IS_ERR(dev_device)) {
        class_destroy(dev_class);
        unregister_chrdev(major_number, DEVICE_NAME);
        printk(KERN_ALERT "XORO: Failed to create dev_device\n");
        return PTR_ERR(dev_device);
    }

    mutex_init(&xoroshiro128p_mutex);

    seed(314159265, 1618033989);  // Initialize PRNG with pi and phi.

    a = kmalloc_array(TEST_LEN, sizeof(*a), GFP_KERNEL);
    if (!a)
        return err;

    for (i = 0; i < TEST_LEN; i++) {
        r = (r * 725861) % 6599;
        a[i] = r;
    }

    sort_impl(a, TEST_LEN, sizeof(*a), cmpint, NULL);

    err = -EINVAL;
    for (i = 0; i < TEST_LEN - 1; i++)
        if (a[i] > a[i + 1]) {
            pr_err("test has failed\n");
            goto exit;
        }
    err = 0;
    pr_info("test passed\n");
exit:
    kfree(a);
    return err;
}

/** @brief Free all module resources.
 *         Not used if part of a built-in driver rather than a LKM.
 */
static void __exit xoro_exit(void)
{
    mutex_destroy(&xoroshiro128p_mutex);

    device_destroy(dev_class, MKDEV(major_number, 0));

    class_unregister(dev_class);
    class_destroy(dev_class);

    unregister_chrdev(major_number, DEVICE_NAME);
}

/** @brief open() syscall.
 *         Increment counter, perform another jump to effectively give each
 *         reader a separate PRNG.
 *  @param inodep Pointer to an inode object (defined in linux/fs.h)
 *  @param filep Pointer to a file object (defined in linux/fs.h)
 */
static int dev_open(struct inode *inodep, struct file *filep)
{
    /* Try to acquire the mutex (returns 0 on fail) */
    if (!mutex_trylock(&xoroshiro128p_mutex)) {
        printk(KERN_INFO "XORO: %s busy\n", DEVICE_NAME);
        return -EBUSY;
    }

    jump(); /* in xoroshiro128plus.c */

    printk(KERN_INFO "XORO: %s opened. n_opens=%d\n", DEVICE_NAME, n_opens++);

    return 0;
}

static ktime_t kt;
/** @brief Called whenever device is read from user space.
 *  @param filep Pointer to a file object (defined in linux/fs.h).
 *  @param buffer Pointer to the buffer to which this function may write data.
 *  @param len Number of bytes requested.
 *  @param offset Unused.
 *  @return Returns number of bytes successfully read. Negative on error.
 */
static ssize_t dev_read(struct file *filep,
                        char *buffer,
                        size_t len,
                        loff_t *offset)
{
    size_t len_ = (len > 8) ? 8 : len;
    uint64_t *times = kmalloc(sizeof(uint64_t) * 8, GFP_KERNEL);

    enum sorttyp {
        k_sort = 0,
        shell_sort,
        binary_insertion_sort,
        heap_sort,
        quick_sort,
        merge_sort,
        selection_sort,
        tim_sort
    };
    // generate data to sort
    uint64_t *src = kmalloc(sizeof(uint64_t) * TEST_LEN, GFP_KERNEL);
    uint64_t *dst = kmalloc(sizeof(uint64_t) * TEST_LEN, GFP_KERNEL);
    for (int i = 0; i < TEST_LEN; i++) {
        src[i] = next();
    }

    memcpy(dst, src, sizeof(uint64_t) * TEST_LEN);
    // measure sorting time
    kt = ktime_get();
    sort_impl(dst, TEST_LEN, sizeof(*src), cmpuint64, NULL);
    times[(int) k_sort] = ktime_sub(ktime_get(), kt);
    printk(KERN_INFO " %llu\n", kt);

#define ESORT 999
    for (int i = 0; i < TEST_LEN - 1; i++)
        if (dst[i] > dst[i + 1]) {
            pr_err("test has failed with k_sort\n");
            kfree(src);
            kfree(dst);
            return -ESORT;
        }


    // shell sort
    memcpy(dst, src, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_shell_sort(dst, TEST_LEN);
    times[(int) shell_sort] = ktime_sub(ktime_get(), kt);
    printk(KERN_INFO " %llu\n", kt);

    for (int i = 0; i < TEST_LEN - 1; i++)
        if (dst[i] > dst[i + 1]) {
            pr_err("test has failed with shell_sort\n");
            kfree(src);
            kfree(dst);
            return -ESORT;
        }

    // binary insertion sort
    memcpy(dst, src, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_binary_insertion_sort(dst, TEST_LEN);
    times[(int) binary_insertion_sort] = ktime_sub(ktime_get(), kt);
    printk(KERN_INFO " %llu\n", kt);

    for (int i = 0; i < TEST_LEN - 1; i++)
        if (dst[i] > dst[i + 1]) {
            pr_err("test has failed with binary_insertion_sort\n");
            kfree(src);
            kfree(dst);
            return -ESORT;
        }


    // heap sort
    memcpy(dst, src, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_heap_sort(dst, TEST_LEN);
    times[(int) heap_sort] = ktime_sub(ktime_get(), kt);
    printk(KERN_INFO " %llu\n", kt);

    for (int i = 0; i < TEST_LEN - 1; i++)
        if (dst[i] > dst[i + 1]) {
            pr_err("test has failed with heap_sort\n");
            kfree(src);
            kfree(dst);
            kfree(times);
            return -ESORT;
        }


    // quick sort
    memcpy(dst, src, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_quick_sort(dst, TEST_LEN);
    times[(int) quick_sort] = ktime_sub(ktime_get(), kt);
    printk(KERN_INFO " %llu\n", kt);

    for (int i = 0; i < TEST_LEN - 1; i++)
        if (dst[i] > dst[i + 1]) {
            pr_err("test has failed with quick_sort\n");
            kfree(src);
            kfree(dst);
            kfree(times);
            return -ESORT;
        }


    // merge sort
    memcpy(dst, src, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_merge_sort(dst, TEST_LEN);
    times[(int) merge_sort] = ktime_sub(ktime_get(), kt);
    printk(KERN_INFO " %llu\n", kt);

    for (int i = 0; i < TEST_LEN - 1; i++)
        if (dst[i] > dst[i + 1]) {
            pr_err("test has failed with merge_sort\n");
            kfree(src);
            kfree(dst);
            kfree(times);
            return -ESORT;
        }

    // selection sort
    memcpy(dst, src, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_selection_sort(dst, TEST_LEN);
    times[(int) selection_sort] = ktime_sub(ktime_get(), kt);
    printk(KERN_INFO " %llu\n", kt);

    for (int i = 0; i < TEST_LEN - 1; i++)
        if (dst[i] > dst[i + 1]) {
            pr_err("test has failed with merge_sort\n");
            kfree(src);
            kfree(dst);
            kfree(times);
            return -ESORT;
        }

    // tim sort
    memcpy(dst, src, sizeof(uint64_t) * TEST_LEN);
    kt = ktime_get();
    ksort_tim_sort(dst, TEST_LEN);
    times[(int) tim_sort] = ktime_sub(ktime_get(), kt);
    printk(KERN_INFO " %llu\n", kt);

    for (int i = 0; i < TEST_LEN - 1; i++)
        if (dst[i] > dst[i + 1]) {
            pr_err("test has failed with merge_sort\n");
            kfree(src);
            kfree(dst);
            kfree(times);
            return -ESORT;
        }

    kfree(src);
    kfree(dst);
    pr_info("test passed\n");
    for (int i = 0; i < 8; ++i) {
        printk(KERN_INFO "%llu ns\n", times[i]);
    }
    /* copy_to_user has the format ( * to, *from, size) and ret 0 on success */
    int n_notcopied =
        copy_to_user(buffer, (void *) (times), sizeof(uint64_t) * 8);

    kfree(times);
    if (0 != n_notcopied) {
        printk(KERN_ALERT "XORO: Failed to read %d/%ld bytes\n", n_notcopied,
               len_);
        return -EFAULT;
    }

    printk(KERN_INFO "XORO: read %ld bytes\n", len_);
    return len_;
}

/** @brief Called when the userspace program calls close().
 *  @param inodep A pointer to an inode object (defined in linux/fs.h)
 *  @param filep A pointer to a file object (defined in linux/fs.h)
 */
static int dev_release(struct inode *inodep, struct file *filep)
{
    mutex_unlock(&xoroshiro128p_mutex);
    return 0;
}

module_init(xoro_init);
module_exit(xoro_exit);

static ssize_t dev_write(struct file *file,
                         const char *buf,
                         size_t size,
                         loff_t *offset)
{
    printk(" %llu\n", kt);
    return ktime_to_ns(kt);
}
