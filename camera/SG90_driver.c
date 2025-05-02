#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/cdev.h>
#include <linux/version.h>
#include "sg90_ioctl.h"


#define DEVICE_NAME "sg90"
#define PWM_PERIOD_NS (20 * 1000 * 1000)      //PWM wave of 20ms = 20 * 10^6 ns(hardware base unit)
#define MIN_DUTY_NS (1 * 1000 * 1000)
#define MAX_DUTY_NS (2 * 1000 * 1000)
#define STEP_SMALL_NS (110 * 1000)
//#define STEP_LARGE_NS (170 * 1000)
#define SMOOTH_STEP_NS 2000

static struct class *sg90_class;
static struct cdev sg90_cdev;
static dev_t sg90_num;

static struct task_struct *pwm_updown;
static struct task_struct *pwm_leftright;

static int gpio_updown = 585;  //gpio pin 14
static int gpio_leftright = 586; //gpio pin 15

static int current_updown_ns = 1.5 * 1000 * 1000;   //init angle of 90 degree by 1.5ms
static int target_updown_ns = 1.5 * 1000 * 1000;
static int current_leftright_ns = 1.5 * 1000 * 1000;
static int target_leftright_ns = 1.5 * 1000 * 1000;

static int pwm_updown_fn(void *data){
    while(!kthread_should_stop()){
        gpio_set_value(gpio_updown,1);
        udelay( current_updown_ns / 1000);
        gpio_set_value(gpio_updown,0);
        udelay( (PWM_PERIOD_NS - current_updown_ns) / 1000);

        if (current_updown_ns < target_updown_ns)
            current_updown_ns += SMOOTH_STEP_NS;
        else if(current_updown_ns > target_updown_ns)
            current_updown_ns -= SMOOTH_STEP_NS;
    }
    return 0;
}

static int pwm_leftright_fn(void *data){
    while(!kthread_should_stop()){
        gpio_set_value(gpio_leftright,1);
        udelay( current_leftright_ns / 1000);
        gpio_set_value(gpio_leftright,0);
        udelay( (PWM_PERIOD_NS - current_leftright_ns) / 1000);

        if (current_leftright_ns < target_leftright_ns)
            current_leftright_ns += SMOOTH_STEP_NS;
        else if(current_leftright_ns > target_leftright_ns)
            current_leftright_ns -= SMOOTH_STEP_NS;
    }
    return 0;
}

static long sg90_ioctl(struct file *file, unsigned int cmd, unsigned long arg){
    switch (cmd)
    {
    case SG90_MOVE_UP:
        target_updown_ns -= STEP_SMALL_NS;
        if(target_updown_ns < MIN_DUTY_NS)
            target_updown_ns = MIN_DUTY_NS;
        break;
    case SG90_MOVE_DOWN:
        target_updown_ns += STEP_SMALL_NS;
        if(target_updown_ns > MAX_DUTY_NS)
            target_updown_ns = MAX_DUTY_NS;
        break;
    case SG90_MOVE_TOP:
        target_updown_ns = MIN_DUTY_NS;
        break;
    case SG90_MOVE_BUTTOM:
        target_updown_ns = MAX_DUTY_NS;
        break;
    case SG90_MOVE_RIGHT:
        target_leftright_ns -= STEP_SMALL_NS;
        if(target_leftright_ns < MIN_DUTY_NS)
            target_leftright_ns = MIN_DUTY_NS;
        break;
    case SG90_MOVE_LEFT:
        target_leftright_ns += STEP_SMALL_NS;
        if(target_leftright_ns > MAX_DUTY_NS)
            target_leftright_ns = MAX_DUTY_NS;
        break;
    case SG90_MOVE_RIGHT_:
        target_leftright_ns = MIN_DUTY_NS;
        break;
    case SG90_MOVE_LEFT_:
        target_leftright_ns = MAX_DUTY_NS;
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static int sg90_open(struct inode *inode, struct file *file){
    printk("SG90 載入!\n");
    return 0;
}
static int sg90_close(struct inode *inode, struct file *file){
    printk("SG90 關閉\n");
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open  = sg90_open,
    .release = sg90_close,
    .unlocked_ioctl = sg90_ioctl,
};

static int __init sg90_init(void){
    
    if(alloc_chrdev_region(&sg90_num, 0, 1,DEVICE_NAME) < 0){
        printk(KERN_ALERT"alloc_chrdev_region failed!\n");
        return -1;
    };

    cdev_init(&sg90_cdev, &fops);
    if(cdev_add(&sg90_cdev, sg90_num, 1) < 0){
        printk(KERN_ALERT"Error adding cdev!\n");
        return -1;
    };
#if LINUX_VERSION_CODE < KERNEL_VERSION(6,4,0)
    sg90_class = class_create(THIS_MODULE, DEVICE_NAME);
#else
    sg90_class = class_create(DEVICE_NAME);
#endif
    if(IS_ERR(sg90_class)){
        printk(KERN_ALERT"Err: class created failed!\n");
        return -1;
    }
    device_create(sg90_class, NULL, sg90_num, NULL, DEVICE_NAME);

    if (gpio_request(gpio_updown, "SG90_UPDOWN") || gpio_direction_output(gpio_updown, 0)) {
        pr_err("Failed to request or configure GPIO12\n");
        return -EINVAL;
    }
    if (gpio_request(gpio_leftright, "SG90_LEFTRIGHT") || gpio_direction_output(gpio_leftright, 0)) {
        gpio_free(gpio_updown);
        pr_err("Failed to request or configure GPIO13\n");
        return -EINVAL;
    }


    current_updown_ns    = target_updown_ns    = 1500 * 1000;
    current_leftright_ns = target_leftright_ns = 1500 * 1000;
    
    pwm_updown = kthread_run(pwm_updown_fn, NULL, "sg90_updown");
    pwm_leftright = kthread_run(pwm_leftright_fn, NULL, "sg90_leftright");
    if(IS_ERR(pwm_updown) || IS_ERR(pwm_leftright)){
        gpio_free(gpio_updown);
        gpio_free(gpio_leftright);
        pr_err("Failed to create PWM threads\n");
        return -ENOMEM;
    }
    
    printk("SG90驅動程式 註冊成功!!\n");
    printk("SG90 轉至degree 90\n");
    printk(KERN_INFO"mknod /dev/sg90 c %d 0\n",MAJOR(sg90_num));
    return 0;
}

static void __exit sg90_exit(void){

    if(pwm_updown)
        kthread_stop(pwm_updown);
    if(pwm_leftright)
        kthread_stop(pwm_leftright);

    gpio_set_value(gpio_updown,0);
    gpio_set_value(gpio_leftright,0);
    gpio_free(gpio_updown);
    gpio_free(gpio_leftright);

    device_destroy(sg90_class, sg90_num);
    class_destroy(sg90_class);
    cdev_del(&sg90_cdev);
    unregister_chrdev_region(sg90_num, 1);

    printk("SG90驅動程式 解除註冊!\n");
}

module_init(sg90_init);
module_exit(sg90_exit);
MODULE_AUTHOR("Mike Liang");
MODULE_DESCRIPTION("Control SG90 to different degree");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL");



