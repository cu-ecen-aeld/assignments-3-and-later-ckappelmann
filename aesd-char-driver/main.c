/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
int aesd_major = 0; // use dynamic major
int aesd_minor = 0;

MODULE_AUTHOR("Christopher Kappelmann"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    struct aesd_dev *dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                  loff_t *f_pos)
{
    ssize_t retval = 0;
    int ret;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle read
     */
    struct aesd_dev *dev = filp->private_data;
    ret = mutex_lock_interruptible(&dev->buffer_mutex);
    if (ret != 0)
    {
        return -ERESTART;
    }
    size_t entry_offset;
    struct aesd_buffer_entry *entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->buffer, *f_pos, &entry_offset);
    if (entry == NULL)
    {
        retval = 0;
        goto cleanup;
    }
    size_t bytes_to_copy = entry->size - entry_offset;
    bytes_to_copy = count < bytes_to_copy ? count : bytes_to_copy;
    if (copy_to_user(buf, entry->buffptr + entry_offset, bytes_to_copy))
    {
        retval = -EFAULT;
        goto cleanup;
    }
    retval = bytes_to_copy;
    *f_pos += bytes_to_copy;

cleanup:
    mutex_unlock(&dev->buffer_mutex);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                   loff_t *f_pos)
{
    int ret;
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    /**
     * TODO: handle write
     */
    struct aesd_dev *dev = filp->private_data;
    ret = mutex_lock_interruptible(&dev->input_buffer_mutex);
    if (ret != 0)
    {
        return -ERESTART;
    }
    size_t new_count = dev->input_buffer_length + count;
    if (dev->input_buffer == NULL)
    {
        dev->input_buffer = kmalloc(count, GFP_KERNEL);
        if (dev->input_buffer == NULL)
        {
            retval = -ENOMEM;
            goto cleanup;
        }
        dev->input_buffer_length = 0;
        dev->input_buffer_capacity = count;
    }
    else if (dev->input_buffer_capacity < new_count)
    {
        char *new_buffer = krealloc(dev->input_buffer, new_count, GFP_KERNEL);
        if (new_buffer == NULL)
        {
            // dev->input_buffer remain valid
            retval = -ENOMEM;
            goto cleanup;
        }
        dev->input_buffer = new_buffer;
        dev->input_buffer_capacity = new_count;
    }
    if (copy_from_user(dev->input_buffer + dev->input_buffer_length, buf, count))
    {
        retval = -EFAULT;
        goto cleanup;
    }
    dev->input_buffer_length += count;

    // See if we can find full packets
    bool found_packet = true;
    while (found_packet)
    {
        size_t i = 0;
        while (i < dev->input_buffer_length)
        {
            if (dev->input_buffer[i] == '\n')
            {
                break;
            }
            i++;
        }
        if (i < dev->input_buffer_length)
        {
            // Write a full packet into the circular buffer
            size_t packet_length = i + 1;
            ret = mutex_lock_interruptible(&dev->buffer_mutex);
            if (ret != 0)
            {
                retval = -ERESTART;
                goto cleanup;
            }
            char *entry_buffer = kmalloc(packet_length, GFP_KERNEL);
            if (entry_buffer == NULL)
            {
                retval = -ENOMEM;
                mutex_unlock(&dev->buffer_mutex);
                goto cleanup;
            }
            memcpy(entry_buffer, dev->input_buffer, packet_length);
            memmove(dev->input_buffer, dev->input_buffer + packet_length, dev->input_buffer_length - packet_length);
            dev->input_buffer_length -= packet_length;
            struct aesd_buffer_entry new_entry = {.buffptr = entry_buffer, .size = packet_length};
            struct aesd_buffer_entry removed_entry = aesd_circular_buffer_add_entry(&dev->buffer, &new_entry);
            mutex_unlock(&dev->buffer_mutex);
            if (removed_entry.buffptr != NULL)
            {
                kfree(removed_entry.buffptr);
                removed_entry.buffptr = NULL;
                removed_entry.size = 0;
            }
        }
        else
        {
            found_packet = false;
        }
    }
    retval = count;

cleanup:
    mutex_unlock(&dev->input_buffer_mutex);
    return retval;
}

struct file_operations aesd_fops = {
    .owner = THIS_MODULE,
    .read = aesd_read,
    .write = aesd_write,
    .open = aesd_open,
    .release = aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add(&dev->cdev, devno, 1);
    if (err)
    {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    return err;
}

int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
                                 "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0)
    {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    mutex_init(&aesd_device.buffer_mutex);
    mutex_init(&aesd_device.input_buffer_mutex);
    aesd_circular_buffer_init(&aesd_device.buffer);
    aesd_device.input_buffer = NULL;
    aesd_device.input_buffer_length = 0;
    aesd_device.input_buffer_capacity = 0;

    result = aesd_setup_cdev(&aesd_device);

    if (result)
    {
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    // Fill the buffer up with NULL entries
    struct aesd_buffer_entry entry = {.buffptr = NULL, .size = 0};
    for (int i = 0; i < sizeof(aesd_device.buffer.entry) / sizeof(aesd_device.buffer.entry[0]); i++)
    {
        struct aesd_buffer_entry old_entry = aesd_circular_buffer_add_entry(&aesd_device.buffer, &entry);
        if (old_entry.buffptr != NULL)
        {
            kfree(old_entry.buffptr);
            old_entry.buffptr = NULL;
            old_entry.size = 0;
        }
    }

    // Free the input buffer
    if(aesd_device.input_buffer != NULL) {
        kfree(aesd_device.input_buffer);
    }

    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
