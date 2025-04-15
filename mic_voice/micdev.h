#ifndef MICDEV_H
#define MICDEV_H
#include <linux/ioctl.h>

#define MAGIC_NUM 3 
#define IOC_MIC_START_RECORD _IOR(MAGIC_NUM,0,char *)
#define IOC_MIC_STOP_RECORD _IO(MAGIC_NUM,1,char *)
#endif