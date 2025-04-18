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

#define DEVICE_NAME "led"
#define MINOR_NUM 2

static struct cdev led_cdev;
static dev_t dev_number;
static struct class *led_class;

struct io_window{
    unsigned long phys_base;
    unsigned long len;
};

uint32_t *PERIBase;
uint32_t *GPIOBase;
uint32_t *RIOBase;
uint32_t *PADBase;
uint32_t *pad;

typedef struct{
    uint32_t status;
    uint32_t ctrl;
}GPIOregs;

#define GPIO ((GPIOregs*)GPIOBase)

typedef struct{
    uint32_t Out;
    uint32_t OE;
    uint32_t In;
    uint32_t InSync;
} rioregs;

#define rio ((rioregs*)RIOBase)
#define rioXOR ((rioregs*)(RIOBase + 0x1000) / 4)
#define rioOSET ((rioregs*)(RIOBase + 0x2000) / 4)
#define rioOCLR ((rioregs*)(RIOBase + 0x3000) / 4)

uint32_t pin_1 = 23;
uint32_t pin_2 = 24;
uint32_t fn = 5;

static struct file_operations fops = {
    .write = led_write,
    .open  = led_open,
    .release = led_release
};

static int __init led_init(void)
{
    int ret;
    ret = (alloc_chrdev_region(&dev_number,0,MINOR_NUM, DEVICE_NAME));
    if(ret < 0){
        printk("註冊字元驅動程式失敗!\n");
        return -1;
    }

    cdev_init(&led_cdev,&fops);
    mic_cdev.owner = THIS_MODULE;
    cdev_add(&led_cdev, dev_number, MINOR_NUM);

    led_class = class_create(DEVICE_NAME);

    
    PERIBase = ioremap(0x1f00000000,64*1024*1024);
    if( PERIBase == NULL ){
        printk("ioremap() fail\n");
    goto failed_ioremap;
    }

    GPIOBase = PERIBase + 0xD000 / 4;
    RIOBase = PERIBase  + 0xe000 / 4;
    PADBase = PERIBase  + 0xf000 / 4;
    pad = PADBase + 1;


    printk("註冊led 成功!\n");
    printk("led major: %d",MAJOR(dev_number));

    return 0;

failed_ioremap:
    device_destroy(led_class, dev_number);
    class_destroy(led_class);
}


static int __exit led_exit(void){

    iounmap(PERIBase);
    device_destroy(led_class, dev_number);
    class_destroy(led_class);
    cdev_del(&led_cdev);
    unregister_chrdev_region(dev_number, MINOR_NUM);

    printk("解除註冊 led!\n");
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");

