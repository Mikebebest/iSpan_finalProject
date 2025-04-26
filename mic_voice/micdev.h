#ifndef MICDEV_H
#define MICDEV_H
#include <linux/ioctl.h>

#define MAGIC_NUM 'm'
#define IOC_MIC_START_RECORD _IOW(MAGIC_NUM,0,int)
#define IOC_MIC_STOP_RECORD _IOW(MAGIC_NUM,1,int)

#define MICDEV_VERSION 0222
#endif