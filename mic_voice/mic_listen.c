#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/ioctl.h>

#define DEVICE_NAME "inmp441_mic"

#define MAGIC_NUM 3 
#define IOCTL_MIC_START_RECORD _IOR(MAGIC_NUM,0,char *)
#define IOCTL_MIC_STOP_RECORD _IO(MAGIC_NUM,1,char *)

static struct cdev mic_cdev;
static dev_t dev_number;
static struct class *mic_class;

struct task_struct *record_thread;
bool is_recording = false;

/*  不需要kthread
int record_fn(void *data){

    printk("開始錄音!\n");

    while(!kthread_should_stop()){

        if(is_recording){

            printk("recording voice....\n");
        }
        msleep(1000);
    }

    printk("停止錄音!\n");
    return 0;
}
*/

static int mic_open( struct inode *inode, struct file *file){
    
    is_recording = true;
    printk("inmp441 麥克風開啟!\n");
    record_thread = kthread_run(record_fn, NULL, "record_thread");
    if(IS_ERR(record_thread)){
        printk("無法建立錄音執行續!\n");
        return PTR_ERR(record_thread);
    }
    return 0;
}

static int mic_release( struct inode *inode, struct file *file){

    is_recording = false;
    kthread_stop(record_thread);
    printk("inmp441 麥克風關閉!\n");
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = mic_open,
//    .unlocked_ioctl = mic_ioctl,
    .release = mic_release
};

static int __init mic_init(void){

    int ret;
    ret = (alloc_chrdev_region(&dev_number,0,1, DEVICE_NAME));
    if(ret < 0){
        printk("註冊字元驅動程式失敗!\n");
        return -1;
    }
    
    cdev_init(&mic_cdev,&fops);
    mic_cdev.owner = THIS_MODULE;
    cdev_add(&mic_cdev, dev_number, 1);

    mic_class = class_create(THIS_MODULE, DEVICE_NAME);
    device_create(mic_class, NULL, dev_number, NULL, DEVICE_NAME);

    printk("註冊 inmp441 mic 成功!\n");
    printk("inmp441: major=%d, minor=%d\n", MAJOR(dev_number), MINOR(dev_number));

    return 0;
}

static void __exit mic_exit(void){
    
    device_destroy(mic_class, dev_number);
    class_destroy(mic_class);
    cdev_del(&mic_cdev);
    unregister_chrdev_region(dev_number, 1);

    printk("解除註冊 inmp441 mic!\n");
}

module_init(mic_init);
module_exit(mic_exit);
MODULE_LICENSE("GPL");