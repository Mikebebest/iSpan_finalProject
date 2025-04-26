#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/poll.h>
#include <linux/io.h>

#define DEVICE_NAME "camera"
#define SUCCESS 0
#define __READL_WRITEL__

static struct cdev cam_cdev;    
static dev_t cam_number;
static struct class *cam_class;

uint32_t *PERIBase;
uint32_t *GPIOBase;

typedef struct{
    uint32_t status;
    uint32_t ctrl;
}GPIOregs;

#define GPIO ((GPIOregs*)GPIOBase)

uint32_t pin1 = 12;
uint32_t pin2 = 13;
unit32_t fn = 0;


static struct file_operations fops = {
    .open = cam_open,
    .release = cam_close,
    .write = cam_write,
    .unlocked_ioctl = cam_ioctl,
};

static int __init cam_init(void){

    int ret;
    ret = (alloc_chrdev_region(&dev_number,0,1, DEVICE_NAME));
    if(ret < 0){
        printk("註冊字元驅動程式失敗!\n");
        return -1;
    }

    cdev_init(&cam_cdev,&fops);
    cam_cdev.owner = THIS_MODULE;
    cdev_add(&cam_cdev, dev_number, 1);

    cam_class = class_create(DEVICE_NAME);

    if(IS_ERR(cam_class)){
        printk(" mic class created failed\n");
        return -1;
    }

    device_create(cam_class, NULL, MKDEV(MAJOR(dev_number), 0), NULL, DEVICE_NAME);
    printk(KERN_ALERT"'mknod /dev/camera c %d 0'.\n", MAJOR(dev_number));

    PERIBase = ioremap(0x1f00000000,64*1024*1024);
    if( PERIBase == NULL ){
        printk("ioremap() fail\n");
        device_destroy(cam_class, dev_number);
        class_destroy(cam_class);
        return -1;;
    }

    GPIOBase = PERIBase + 0xD0000 / 4;
}

static void __exit cam_exit(void){

}

module_init(cam_init);
module_exit(cam_exit);
MODULE_LICENSE("GPL");

