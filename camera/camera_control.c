#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/types.h>
#include "sg90_ioctl.h"

#define PORT 7000
#define DEV_SG "/dev/sg90"

int sg90_fd;
int keep_running = 1;

void sig_handler(int signo) {
    printf("\n🛑 Received signal CTRL+C\n");
    keep_running = 0;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };
    connect(sock, (struct sockaddr *)&addr, sizeof(addr)); // fake 連線
    close(sock);
}

void handle_move(const char *args){
    if(strcmp(args, "up") == 0){
        ioctl(sg90_fd,SG90_MOVE_UP);
        printf("sg90 moving up!\n");
    }else if(strcmp(args, "down") == 0){
        ioctl(sg90_fd,SG90_MOVE_DOWN);
        printf("sg90 moving down!\n");
    }else if(strcmp(args, "left") == 0){
        ioctl(sg90_fd,SG90_MOVE_LEFT);
        printf("sg90 moving left!\n");
    }else if(strcmp(args, "right") == 0){
        ioctl(sg90_fd,SG90_MOVE_RIGHT);
        printf("sg90 moving right!\n");
    }else{
        printf("unknown command: %s\n",args);
    }
}


void handle_capture(const char *args, int client_fd){
    unsigned int w, h;
    sscanf(args,"%u %u",&w,&h);
    printf("📷 Capture request: %ux%u\n", w, h);

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
        "libcamera-still -o output.jpg --width %u --height %u --nopreview",
        w, h);
    system(cmd);

    write(client_fd, "OK\n", 3); // 新增：先傳 OK

    int img_fd = open("output.jpg", O_RDONLY);
    if (img_fd < 0) return;
    char img_buf[4096];
    ssize_t n;
    while ((n = read(img_fd, img_buf, sizeof(img_buf))) > 0) {
        write(client_fd, img_buf, n);
    }
    close(img_fd);
}

int main() {
    int server_fd, client_fd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    char buffer[256];

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

    sg90_fd = open(DEV_SG,O_RDONLY);
    if(sg90_fd < 0 ){
        perror("sg90 device open failed!");
        return 1;
    }
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 5);

    printf("🎯 Pi Control Server listening on port %d\n", PORT);

    while (keep_running) {
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
            handle_capture(args,client_fd);
        }else if(strcmp(cmd, "move") == 0 ){
            handle_move(args);
            write(client_fd,"OK\n",3);
        }else{
            write(client_fd,"unknown cmd\n",12);
        }
        close(client_fd);
    }

    printf("\n🛑 Shutting down server...\n");
    close(server_fd);
    close(sg90_fd);
    printf("Goodbye!\n");
    return 0;
}
