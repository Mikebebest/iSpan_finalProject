#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/i2c.h>
#include "camera_ioctl.h"

#define DEVICE_NAME "my_camera"

static struct class *camera_class;
static struct cdev camera_cdev;
static dev_t camera_dev;
static struct i2c_client *camera_client;

static int camera_i2c_write(u16 reg, u8 val)
{
    if (!camera_client) {
        printk(KERN_ERR "camera_i2c_write: camera_client NULL\n");
        return -ENODEV;
    }

    u8 buf[3];
    struct i2c_msg msg;
    buf[0] = (reg >> 8) & 0xFF;
    buf[1] = reg & 0xFF;
    buf[2] = val;

    msg.addr = camera_client->addr;
    msg.flags = 0;
    msg.len = 3;
    msg.buf = buf;

    return i2c_transfer(camera_client->adapter, &msg, 1);
}

static long camera_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct cam_resolution res;

    switch (cmd) {
    case CAM_IOCTL_SET_RESOLUTION:
        if (copy_from_user(&res, (void __user *)arg, sizeof(res)))
            return -EFAULT;

        printk(KERN_INFO "Camera: Set resolution %ux%u\n", res.width, res.height);

        if (res.width == 1280 && res.height == 720) {
            camera_i2c_write(0x3808, 0x05);
            camera_i2c_write(0x3809, 0x00);
            camera_i2c_write(0x380a, 0x02);
            camera_i2c_write(0x380b, 0xD0);
        } else if (res.width == 1920 && res.height == 1080) {
            camera_i2c_write(0x3808, 0x07);
            camera_i2c_write(0x3809, 0x80);
            camera_i2c_write(0x380a, 0x04);
            camera_i2c_write(0x380b, 0x38);
        } else {
            printk(KERN_ERR "Unsupported resolution\n");
            return -EINVAL;
        }
        break;

    default:
        return -ENOTTY;
    }

    return 0;
}

static const struct file_operations camera_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = camera_ioctl,
};

static int camera_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
    camera_client = client;
    printk(KERN_INFO "Camera sensor probed\n");
    return 0;
}

static const struct i2c_device_id camera_id[] = {
    { "ov5647", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, camera_id);

static struct i2c_driver camera_i2c_driver = {
    .driver = {
        .name = "my_camera_driver",
    },
    .probe = camera_probe,
    .id_table = camera_id,
};

static int __init camera_init(void)
{
    alloc_chrdev_region(&camera_dev, 0, 1, DEVICE_NAME);
    camera_class = class_create(DEVICE_NAME);
    device_create(camera_class, NULL, camera_dev, NULL, DEVICE_NAME);

    cdev_init(&camera_cdev, &camera_fops);
    cdev_add(&camera_cdev, camera_dev, 1);

    i2c_add_driver(&camera_i2c_driver);

    printk(KERN_INFO "Camera driver loaded\n");
    return 0;
}

static void __exit camera_exit(void)
{
    i2c_del_driver(&camera_i2c_driver);

    cdev_del(&camera_cdev);
    device_destroy(camera_class, camera_dev);
    class_destroy(camera_class);
    unregister_chrdev_region(camera_dev, 1);

    printk(KERN_INFO "Camera driver unloaded\n");
}

module_init(camera_init);
module_exit(camera_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("YourName");
MODULE_DESCRIPTION("Simple Camera Resolution Control Driver with I2C - Raspberry Pi Kernel Compatible");