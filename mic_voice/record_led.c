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
#define SUCCESS 0
#define __READL_WRITEL__

static struct cdev led_cdev;
static dev_t dev_number;
static struct class *led_class;
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

static int led_open(struct inode* inode, struct file *file){
    printk("開啟led 設定 GPIO23 24 as RIO\n");

    writel(fn, &GPIO[pin_1].ctrl);
    writel(fn, &GPIO[pin_2].ctrl);
    writel(0x10, &pad[pin_1]);
    writel(0x10, &pad[pin_2]);
    writel(0x01<<pin_1, &rioSET->OE);
    writel(0x01<<pin_2, &rioSET->OE);
    return SUCCESS;
}

static int led_close(struct inode* inode, struct file *file){
    u32 val;
    val = readl(&rioSET->OE);
    val &= ~(1<<pin_1);
    val &= ~(1<<pin_2);
    writel(val, &rioSET->OE);
    return 0;
}

static ssize_t led_write(struct file *fp, const char *buff, size_t len, loff_t *offset){
    int byte_written = 0;
    char buffer[80];
    if(copy_from_user(buffer, buff, len) != 0){
        return -EFAULT;
    }
    sscanf(buffer,"%d",&led_1_2);
    if(led_1_2 == 1){
        writel(1<<pin_2, &rioCLR->Out);
        writel(1<<pin_1, &rioSET->Out);
    }else if (led_1_2 == 2){
        writel(1<<pin_1, &rioCLR->Out);
        writel(1<<pin_2, &rioSET->Out);
    }else if(led_1_2 == 0){
        writel(1<<pin_1,&rioCLR->Out);
        writel(1<<pin_2,&rioCLR->Out);
    }else{
        return -EINVAL;
    }
    byte_written++;
    return byte_written;
}

static struct file_operations fops = {
    .write = led_write,
    .open  = led_open,
    .release = led_close,
};

static int __init led_init(void)
{
    int ret;
    ret = (alloc_chrdev_region(&dev_number,0,1, DEVICE_NAME));
    if(ret < 0){
        printk("註冊字元驅動程式失敗!\n");
        return -1;
    }

    cdev_init(&led_cdev,&fops);
    led_cdev.owner = THIS_MODULE;
    cdev_add(&led_cdev, dev_number, 1);

    led_class = class_create(DEVICE_NAME);

    if(IS_ERR(led_class)){
        printk("led class created failed\n");
        return -1;
    }
    device_create(led_class, NULL, MKDEV(MAJOR(dev_number), 0), NULL, DEVICE_NAME);
    printk(KERN_ALERT"'mknod /dev/led c %d 0'.\n", MAJOR(dev_number));

    PERIBase = ioremap(0x1f00000000,64*1024*1024);
    if( PERIBase == NULL ){
        printk("ioremap() fail\n");
        device_destroy(led_class, dev_number);
        class_destroy(led_class);
        return -1;;
    }

    GPIOBase = PERIBase + 0xD0000 / 4;
    RIOBase = PERIBase  + 0xe0000 / 4;
    PADBase = PERIBase  + 0xf0000 / 4;
    pad = PADBase + 1;


    printk("註冊led 成功!\n");
    printk("led major: %d\n",MAJOR(dev_number));
    led_1_2 = 0;
    return 0;  
}


static void __exit led_exit(void){

    iounmap(PERIBase);
    device_destroy(led_class, dev_number);
    class_destroy(led_class);
    cdev_del(&led_cdev);
    unregister_chrdev_region(dev_number,1);

    printk("解除註冊 led!\n");
}

module_init(led_init);
module_exit(led_exit);
MODULE_LICENSE("GPL");

