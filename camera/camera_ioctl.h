#ifndef CAMERA_IOCTL_H
#define CAMERA_IOCTL_H

#include <linux/ioctl.h>

#define CAM_MAGIC 'c'
#define CAM_IOCTL_SET_RESOLUTION _IOW(CAM_MAGIC, 0, struct cam_resolution)

struct cam_resolution {
    unsigned int width;
    unsigned int height;
};

#endif // CAMERA_IOCTL_H
