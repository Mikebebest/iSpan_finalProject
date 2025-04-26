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
#include "micdev.h"

#define DEVICE_NAME "inmp441_mic"
#define SUCCESS 0
#define __READL_WRITEL__

static struct cdev mic_cdev;    
static dev_t dev_number;
static struct class *mic_class;
static int led_1_2 = 0;

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
#define rioXOR ((rioregs*)(RIOBase + 0x1000 / 4))
#define rioSET ((rioregs*)(RIOBase + 0x2000 / 4))
#define rioCLR ((rioregs*)(RIOBase + 0x3000 / 4))

uint32_t pin_1 = 23;   //red light = recording
uint32_t pin_2 = 24;   //green light = not recording
uint32_t fn = 5;

static int mic_open(struct inode* inode, struct file *file){
    printk("開啟 mic!! \n");
    printk("開啟led 設定 GPIO23 24 as RIO\n");
    writel(fn, &GPIO[pin_1].ctrl);
    writel(fn, &GPIO[pin_2].ctrl);
    writel(0x10, &pad[pin_1]);
    writel(0x10, &pad[pin_2]);
    writel(0x01<<pin_1, &rioSET->OE);
    writel(0x01<<pin_2, &rioSET->OE);
    return SUCCESS;
}

static int mic_close(struct inode* inode, struct file *file){
    u32 val;

    printk("關閉 GPIO 23 24 設定\n");
    printk("關閉 mic!! \n");
    val = readl(&rioSET->OE);
    val &= ~(1<<pin_1);
    val &= ~(1<<pin_2);
    writel(val, &rioSET->OE);
    return 0;
}

/*
static ssize_t mic_write(struct file *fp, const char *buff, size_t len, loff_t *offset){
    int val = 0;
    char buffer[80];
    if(copy_from_user(buffer, buff, len) != 0){
        return -EFAULT;
    }
    if (sscanf(buffer, "%d", &val) == 1) {
        led_1_2 = val;
        return len;
    }

    pr_err("mic_write: invalid input, expected number\n");
    return -EINVAL;
}
*/

static long mic_ioctl (struct file *file, unsigned int cmd, unsigned long ioctl_param){
    int val = (int)ioctl_param;

    switch (cmd)
    {
        case IOC_MIC_START_RECORD:
            led_1_2 = val;
            if (led_1_2 == 1) {
                writel(1 << pin_2, &rioCLR->Out); // 關綠燈
                writel(1 << pin_1, &rioSET->Out); // 開紅燈
            }
            break;

        case IOC_MIC_STOP_RECORD:
            led_1_2 = val;
            if (led_1_2 == 2) {
                writel(1 << pin_1, &rioCLR->Out); // 關紅燈
                writel(1 << pin_2, &rioSET->Out); // 開綠燈
            }
            break;

        default:
            led_1_2 = 0;
            writel(1 << pin_1, &rioCLR->Out); // 全部關
            writel(1 << pin_2, &rioCLR->Out);
            break;
    }
    return 0;
}

static struct file_operations fops = {
//    .write = mic_write,
    .open  = mic_open,
    .release = mic_close,
    .unlocked_ioctl = mic_ioctl,
};

static int __init mic_init(void)
{
    int ret;
    ret = (alloc_chrdev_region(&dev_number,0,1, DEVICE_NAME));
    if(ret < 0){
        printk("註冊字元驅動程式失敗!\n");
        return -1;
    }

    cdev_init(&mic_cdev,&fops);
    mic_cdev.owner = THIS_MODULE;
    cdev_add(&mic_cdev, dev_number, 1);

    mic_class = class_create(DEVICE_NAME);

    if(IS_ERR(mic_class)){
        printk(" mic class created failed\n");
        return -1;
    }
    device_create(mic_class, NULL, MKDEV(MAJOR(dev_number), 0), NULL, DEVICE_NAME);
    printk(KERN_ALERT"'mknod /dev/inmp441_mic c %d 0'.\n", MAJOR(dev_number));

    PERIBase = ioremap(0x1f00000000,64*1024*1024);
    if( PERIBase == NULL ){
        printk("ioremap() fail\n");
        device_destroy(mic_class, dev_number);
        class_destroy(mic_class);
        return -1;;
    }

    GPIOBase = PERIBase + 0xD0000 / 4;
    RIOBase = PERIBase  + 0xe0000 / 4;
    PADBase = PERIBase  + 0xf0000 / 4;
    pad = PADBase + 1;


    printk("註冊inmp441 mic 成功!\n");
    printk("mic major: %d\n",MAJOR(dev_number));
    led_1_2 = 0;
    return 0;  
}


static void __exit mic_exit(void){

    iounmap(PERIBase);
    device_destroy(mic_class, dev_number);
    class_destroy(mic_class);
    cdev_del(&mic_cdev);
    unregister_chrdev_region(dev_number,1);

    printk("解除註冊 mic!\n");
}

module_init(mic_init);
module_exit(mic_exit);
MODULE_LICENSE("GPL");

