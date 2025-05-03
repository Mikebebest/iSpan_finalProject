#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/io.h>
#include <linux/uaccess.h>
#include <linux/cdev.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#define DEVICE_NAME "buzzer"

#define BUZZER_ON  _IO('s',0)
#define BUZZER_OFF _IO('s',1)

static struct cdev buzzer_cdev;
static struct class *buzzer_class;
static struct gpio_desc *buzzer_gpio;
static dev_t dev_num;
static int buzzer_on_off = 0;



static int buzzer_open(struct inode *inode, struct file *file){
    printk("buzzer 開啟\n");
    return 0;
}
static int buzzer_close(struct inode *inode, struct file *file){
    printk("buzzer 關閉\n");
    return 0;
}
static ssize_t buzzer_write(struct file *fp, const char *buffer, size_t len, loff_t *off)
{
    int bytes_written = 0;
    char pwbuf[80];

    if(copy_from_user(pwbuf, buffer, len) !=0 ){
        printk("buffer write(): len:%lu\n", len);
        return -EFAULT;
    }
    sscanf(pwbuf,"%d",&buzzer_on_off);
    if( buzzer_on_off)
        gpiod_set_value(buzzer_gpio, 1);
    else   
        gpiod_set_value(buzzer_gpio, 0);

    bytes_written++;
    return bytes_written;
}

static long buzzer_ioctl(struct file *file, unsigned int cmd, unsigned long ioctl_param)
{
    switch(cmd)
    {
        case BUZZER_ON:
            gpiod_set_value(buzzer_gpio, 1);
            break;
        case BUZZER_OFF:
            gpiod_set_value(buzzer_gpio, 0);
            break;
        default:
            return -EINVAL;
    }
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = buzzer_open,
    .release = buzzer_close,
    .write = buzzer_write,
    .unlocked_ioctl = buzzer_ioctl,
};

static int buzzer_probe (struct platform_device *pdev)
{
    int ret;
    struct device *dev = &pdev->dev;

    printk(KERN_INFO"buzzer 裝置已經被probed!\n");
    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if(ret < 0){
        printk(KERN_ALERT"alloc chrdev region failed!\n");
        return -1;
    }
    cdev_init(&buzzer_cdev, &fops);
    buzzer_cdev.owner = THIS_MODULE;
    ret = cdev_add(&buzzer_cdev, dev_num, 1);
    if(ret){
        printk(KERN_ALERT"Error adding cdev\n");
        return -1;
    }
    buzzer_class = class_create("buzzer_class");
    if(IS_ERR(buzzer_class)){
        printk(KERN_ALERT"Err: created class failed!\n");
        return -1;
    }
    device_create(buzzer_class, NULL, dev_num, NULL, DEVICE_NAME);
    printk(KERN_ALERT"mknod /dev/buzzer c %d 0\n",MAJOR(dev_num));
    buzzer_gpio = devm_gpiod_get(dev, NULL, GPIOD_OUT_LOW);
    if(IS_ERR(buzzer_gpio)){
        printk(KERN_ALERT"failed to setup buzzer gpio!\n");
        return PTR_ERR(buzzer_gpio);
    }
    return 0;
}

static int buzzer_remove(struct platform_device *pdev)
{
    pr_info("buzzer 裝置已被移除\n");
    device_destroy(buzzer_class, dev_num);
    class_destroy(buzzer_class);
    cdev_del(&buzzer_cdev);
    unregister_chrdev_region(dev_num,1);

    return 0;
}

static struct of_device_id buzzer_id_tables [] = {
    { .compatible = "buzzer-gpio",},
    {}
};

static struct platform_driver buzzer_drv = {
    .probe = buzzer_probe,
    .remove = buzzer_remove,
    .driver = {
        .name = "buzzer-dtoverlay",
        .of_match_table = buzzer_id_tables,
    },
};

module_platform_driver(buzzer_drv);
MODULE_AUTHOR("Mike Liang");
MODULE_DESCRIPTION("control the buzzer");
MODULE_VERSION("0.0.1");
MODULE_LICENSE("GPL");