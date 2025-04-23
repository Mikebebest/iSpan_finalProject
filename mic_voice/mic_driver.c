#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/kfifo.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>
#include "micdev.h"

#define DEVICE_NAME "inmp441_mic"
#define BUF_SIZE (256 * 1024)

static struct cdev mic_cdev;
static dev_t dev_num;
static struct class *mic_class;
static DECLARE_KFIFO(audio_fifo, char, BUF_SIZE);
static struct task_struct *record_thread;
static struct snd_pcm_substream *target_substream = NULL;

//==================== Helper to locate substream ====================//
static struct snd_pcm_substream *find_substream(const char *cardname, int device, int stream) {
    struct snd_card *card;
    struct snd_pcm *pcm;
    struct snd_pcm_substream *substream = NULL;

    for (int idx = 0; idx < SNDRV_CARDS; idx++) {
        if (snd_card_ref(idx, &card) < 0)
            continue;
        list_for_each_entry(pcm, &card->devices, list) {
            if (pcm->device == device) {
                if (stream == SNDRV_PCM_STREAM_CAPTURE)
                    substream = pcm->streams[stream].substream;
                snd_card_unref(card);
                return substream;
            }
        }
        snd_card_unref(card);
    }
    return NULL;
}

void set_mic_substream(const char *cardname, int device) {
    target_substream = find_substream(cardname, device, SNDRV_PCM_STREAM_CAPTURE);
    if (target_substream)
        pr_info("[mic_driver] Substream found and set.\n");
    else
        pr_err("[mic_driver] Failed to find substream.\n");
}
EXPORT_SYMBOL(set_mic_substream);

//==================== ALSA buffer reader thread ====================//
static int record_fn(void *data){
    struct snd_pcm_runtime *runtime;
    size_t buf_len;
    char *buf;

    while(!kthread_should_stop()){
        if(target_substream && target_substream->runtime && target_substream->runtime->dma_area){
            runtime = target_substream->runtime;
            buf_len = snd_pcm_lib_buffer_bytes(target_substream);
            buf = (char *)runtime->dma_area;
            kfifo_in(&audio_fifo, buf, min(buf_len,(size_t)BUF_SIZE));
            msleep(50);
        }else{
            msleep(100);
        }
    }
    return 0;
}

static int mic_open( struct inode *inode, struct file *file){
    printk(KERN_INFO "開啟 inmp441麥克風!\n");   
    return 0;
}
static int mic_close( struct inode *inode, struct file *file){
    printk(KERN_INFO"關閉 inmp441麥克風!\n");
    return 0;
}

static ssize_t mic_read( struct file *fp, char __user *buffer, size_t len, loff_t *offset){
    unsigned int read_bytes;
    if(kfifo_is_empty(&audio_fifo)) return 0;
    if(kfifo_to_user(&audio_fifo, buffer, len, &read_bytes))
        return -EFAULT;
    return read_bytes;
}

static long mic_ioctl(struct file *file, unsigned int cmd, unsigned long ioctl_param){
    switch (cmd)
    {
    case IOC_MIC_START_RECORD:
        kfifo_reset(&audio_fifo);
        if(!record_thread || IS_ERR(record_thread))
            record_thread = kthread_run(record_fn, NULL, "mic_record");
        break;
    case IOC_MIC_STOP_RECORD:
        if(record_thread){
            kthread_stop(record_thread);
            record_thread = NULL;
        }
        break;
    default:
        return -EINVAL;
    }
    return 0;
}

static struct file_operations fops = {
    .owner = THIS_MODULE,
    .open  = mic_open,
    .read  = mic_read,
    .release = mic_close,
    .unlocked_ioctl = mic_ioctl,
}


static int __init mic_init(void){

    if(alloc_chrdev_region(&dev_num,0,1,DEVICE_NAME) < 0){
        printk("註冊字元驅動程式失敗!\n");
        return -1;
    }
    
    cdev_init(&mic_cdev,&fops);
    mic_cdev.owner = THIS_MODULE;
    cdev_add(&mic_cdev, dev_num, 1);

    mic_class = class_create(DEVICE_NAME);
    device_create(mic_class, NULL, dev_num, NULL, DEVICE_NAME);
    INIT_KFIFO(audio_fifo);

    printk("註冊 inmp441 mic 成功!\n");
    printk("inmp441: major=%d, minor=%d\n", MAJOR(dev_num), MINOR(dev_num));

    return 0;
}

static void __exit mic_exit(void){
    if(record_thread && !IS_ERR(record_thread))
        kthread_stop(record_thread);
    device_destroy(mic_class, dev_num);
    class_destroy(mic_class);
    cdev_del(&mic_cdev);
    unregister_chrdev_region(dev_num, 1);

    printk("解除註冊 inmp441 mic!\n");

}

module_init(mic_init);
module_exit(mic_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Char driver to capture audio from ALSA buffer");