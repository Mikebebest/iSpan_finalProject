#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/types.h>
#include <linux/i2c-dev.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <netinet/in.h>
#include "sg90_ioctl.h"
#include "camera_ioctl.h"

#define OUTPUT_PATH "./uploads/output.jpg"
#define SNAPSHOT_DIR "./uploads/snapshots"
#define RES_CAPTURE_FLAG "res_capture.flag"
#define TAKE_SNAPSHOT_FLAG "take_snapshot.flag"
#define PORT 7000
#define DEV_SG "/dev/sg90"
#define DEV_CAM "/dev/my_camera"

volatile sig_atomic_t running = 1;
volatile sig_atomic_t stream_running = 1;
int sg90_fd;
pthread_t stream_thread;

void save_snapshot();
void capture_resolution_snapshot(const char *res);
void *stream_loop(void *arg);
void sigint_handler(int signo);
void *tcp_server_loop(void *arg);
void handle_move(const char *args);
void handle_capture(const char *args, int client_fd);
void pause_streaming();
void resume_streaming();

int main() {
    signal(SIGINT,  sigint_handler);
    signal(SIGTERM, sigint_handler);

    system("mkdir -p ./uploads");
    system("mkdir -p " SNAPSHOT_DIR);
    system("chmod -R 666 ./uploads");

    sg90_fd = open(DEV_SG,O_RDONLY);
    if(sg90_fd < 0 ){
        perror("sg90 device open failed!");
        return 1;
    }

    pthread_create(&stream_thread, NULL, stream_loop, NULL);
    pthread_t tcp_thread;
    pthread_create(&tcp_thread, NULL, tcp_server_loop, NULL);

    while (running) {
        if (access(TAKE_SNAPSHOT_FLAG, F_OK) == 0) {
            save_snapshot();
            remove(TAKE_SNAPSHOT_FLAG);
        }

        if (access(RES_CAPTURE_FLAG, F_OK) == 0) {
            FILE *fp = fopen(RES_CAPTURE_FLAG, "r");
            if (fp) {
                char res[64];
                if (fgets(res, sizeof(res), fp)) {
                    res[strcspn(res, "\n")] = 0;
                    capture_resolution_snapshot(res);
                }
                fclose(fp);
                remove(RES_CAPTURE_FLAG);
            }
        }
        usleep(100000);
    }

    pthread_join(stream_thread, NULL);
    pthread_join(tcp_thread, NULL);
    close(sg90_fd);
    return 0;
}

void pause_streaming() {
    stream_running = 0;
    system("pkill -9 libcamera-vid");
    system("pkill -9 ffmpeg");
    usleep(1000000);
}

void resume_streaming() {
    if (!stream_running) {
        stream_running = 1;
        pthread_create(&stream_thread, NULL, stream_loop, NULL);
        pthread_detach(stream_thread);
    }
}

void *stream_loop(void *arg) {
    while (running && stream_running) {
        const char *cmd = "libcamera-vid -t 500 --width 640 --height 480 --codec mjpeg -o - | ffmpeg -hide_banner -loglevel error -y -i - -vframes 1 " OUTPUT_PATH;
        int result = system(cmd);
        if (result != 0) {
            fprintf(stderr, "Failed to stream frame.\n");
            break;
        }
        usleep(100000);
    }
    return NULL;
}

void sigint_handler(int signo) {
    printf("\nReceived signal, exiting...\n");
    running = 0;
    stream_running = 0;
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };
    connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);
}

void save_snapshot() {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char filename[256];
    snprintf(filename, sizeof(filename), SNAPSHOT_DIR "/snapshot_%04d%02d%02d_%02d%02d%02d.jpg",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    char command[512];
    snprintf(command, sizeof(command), "cp %s %s", OUTPUT_PATH, filename);
    int result = system(command);
    if (result == 0) {
        printf("Snapshot saved: %s\n", filename);
    } else {
        perror("Failed to save snapshot");
    }
}

void capture_resolution_snapshot(const char *res) {
    pause_streaming();

    int w = 0, h = 0;
    if (sscanf(res, "%dx%d", &w, &h) != 2 || (w != 1280 && w != 1920)) {
        fprintf(stderr, "Invalid or unsupported resolution: %s\n", res);
        resume_streaming();
        return;
    }
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char filename[256];
    snprintf(filename, sizeof(filename), SNAPSHOT_DIR "/photo_%04d%02d%02d_%02d%02d%02d.jpg",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "libcamera-still -o %s --width %d --height %d --nopreview -t 300", filename, w, h);
    int ret = system(cmd);
    if (ret == 0) {
        printf("Resolution snapshot saved: %s\n", filename);
    } else {
        fprintf(stderr, "Failed to take resolution snapshot\n");
    }

    resume_streaming();
}

void *tcp_server_loop(void *arg) {
    int server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buffer[256];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);
    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("Pi Control Server listening on port %d\n", PORT);
    while (running) {
        client_fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
        memset(buffer, 0, sizeof(buffer));
        read(client_fd, buffer, sizeof(buffer));
        char *cmd = strtok(buffer, " \n");
        char *args = strtok(NULL, "\n");
        if (!cmd) {
            close(client_fd);
            continue;
        }
        if(strcmp(cmd,"capture") == 0){
            handle_capture(args, client_fd);
        }else if(strcmp(cmd, "move") == 0 ){
            handle_move(args);
            write(client_fd,"OK\n",3);
        }else{
            write(client_fd,"unknown cmd\n",12);
        }
        close(client_fd);
    }
    close(server_fd);
    return NULL;
}

void handle_move(const char *args){
    int ret = -1;
    if(strcmp(args, "up") == 0){
        ret = ioctl(sg90_fd,SG90_MOVE_UP);
        printf("sg90 moving up!\n");
    }else if(strcmp(args, "down") == 0){
        ret = ioctl(sg90_fd,SG90_MOVE_DOWN);
        printf("sg90 moving down!\n");
    }else if(strcmp(args, "left") == 0){
        ret = ioctl(sg90_fd,SG90_MOVE_LEFT);
        printf("sg90 moving left!\n");
    }else if(strcmp(args, "right") == 0){
        ret = ioctl(sg90_fd,SG90_MOVE_RIGHT);
        printf("sg90 moving right!\n");
    }else{
        printf("unknown command: %s\n",args);
    }
    if (ret < 0) perror("SG90 move ioctl failed");
}

void handle_capture(const char *args, int client_fd){
    pause_streaming();

    unsigned int w, h;
    sscanf(args,"%u %u",&w,&h);
    printf("Capture request: %ux%u\n", w, h);
    int cam_fd = open(DEV_CAM, O_RDWR);
    if (cam_fd >= 0) {
        struct cam_resolution res = { .width = w, .height = h };
        if (ioctl(cam_fd, CAM_IOCTL_SET_RESOLUTION, &res) < 0) {
            perror("Failed to set resolution via ioctl");
        } else {
            printf("Resolution set via ioctl\n");
        }
        close(cam_fd);
    } else {
        perror("Failed to open camera device");
    }
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char filename[256];
    snprintf(filename, sizeof(filename), SNAPSHOT_DIR "/photo_%04d%02d%02d_%02d%02d%02d.jpg",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
             t->tm_hour, t->tm_min, t->tm_sec);
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "libcamera-still -o %s --width %u --height %u --nopreview",
        filename, w, h);
    int ret = system(cmd);
    if (ret != 0) {
        perror("libcamera-still failed");
        resume_streaming();
        return;
    }
    write(client_fd, "OK\n", 3);
    int img_fd = open(filename, O_RDONLY);
    if (img_fd < 0) {
        perror("failed to open captured image");
        resume_streaming();
        return;
    }
    char img_buf[4096];
    ssize_t n;
    while ((n = read(img_fd, img_buf, sizeof(img_buf))) > 0) {
        write(client_fd, img_buf, n);
    }
    close(img_fd);

    resume_streaming();
}