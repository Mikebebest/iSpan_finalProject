#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define PORT 7000

int main() {
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

    printf("🎯 Pi Control Server listening on port %d\n", PORT);

    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&addr, &addrlen);
        memset(buffer, 0, sizeof(buffer));
        read(client_fd, buffer, sizeof(buffer));

        unsigned int width = 1280, height = 720;
        if (strncmp(buffer, "capture", 7) == 0) {
            sscanf(buffer + 7, "%u %u", &width, &height);
            printf("📷 Capture request: %ux%u\n", width, height);

            char cmd[256];
            snprintf(cmd, sizeof(cmd),
                "libcamera-still -o output.jpg --width %u --height %u --nopreview",
                width, height);
            system(cmd);

            write(client_fd, "OK\n", 3); // 新增：先傳 OK

            int img_fd = open("output.jpg", O_RDONLY);
            if (img_fd >= 0) {
                char img_buf[4096];
                ssize_t n;
                while ((n = read(img_fd, img_buf, sizeof(img_buf))) > 0) {
                    write(client_fd, img_buf, n);
                }
                close(img_fd);
            }
        }
        close(client_fd);
    }

    close(server_fd);
    return 0;
}
