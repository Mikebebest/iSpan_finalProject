#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>

#include "micdev.h"

#define FIFO_NAME "./record/inmp_rec"
#define MIC_DEVICE "/dev/inmp441_mic"

volatile int keep_running = 1;
volatile pid_t child_pid = -1;
int mic_fd = -1;
int round_counter = 0;

void create_fifo_if_need();
void signal_handler(int sig);
void cleanup_on_exit();
void safe_sleep(int seconds);

int main() {
    setsid(); // 新增，將自己變成新的 session leader

    int lock_fd = open("/tmp/mic_control.lock", O_CREAT | O_RDWR, 0666);
    if (flock(lock_fd, LOCK_EX | LOCK_NB) != 0) {
        printf("🛑 mic_control4 已在執行中，結束啟動\n");
        exit(1);
    }

    signal(SIGINT, signal_handler);
    atexit(cleanup_on_exit);

    system("mkdir -p ./record");
    create_fifo_if_need();

    mic_fd = open(MIC_DEVICE, O_RDWR);
    if (mic_fd < 0) {
        perror("❌ 無法開啟 MIC 裝置");
        return -1;
    }

    ioctl(mic_fd, IOC_MIC_STOP_RECORD, 2);
    printf("錄音準備.....\n");
    printf("📂 控制程式寫入 FIFO: %s\n", FIFO_NAME);
    sleep(3);

    while (keep_running) {
    //    round_counter++;
    //    printf("\n\U0001F680 開始第 %02d 輪錄音...\n", round_counter);

        child_pid = fork();
        ioctl(mic_fd,IOC_MIC_START_RECORD,1);
        if (child_pid == 0) {
            execlp("sh", "sh", "-c", "arecord -D plughw:2,0 -f S16_LE -c 1 -r 16000 -t raw -q | tee ./record/inmp_rec > /dev/null", NULL);
            perror("❌ arecord|tee exec 失敗");
            exit(1);
        }
        
        while(keep_running){
            printf("[MIC] 錄音中.....\n");
            sleep(5);
        }

    /*    for (int i = 0; i < 50; i++) {
            if (!keep_running) break;
            printf("[錄音中] %02d / 50 秒\r", i + 1);
            fflush(stdout);
            safe_sleep(1);
        }
    
        if (child_pid > 0) {
            kill(child_pid, SIGTERM);
            waitpid(child_pid, NULL, WNOHANG);
            child_pid = -1;
        }

        printf("\n[DEBUG] 錄音結束，準備切換\n");

        ioctl(mic_fd, IOC_MIC_STOP_RECORD, 2);

        FILE *fp = fopen(FIFO_NAME, "w");
        if (fp) {
            fprintf(fp, "__STOP__\n");
            fclose(fp);
            printf("📤 傳送 __STOP__ 至 Python\n");
        }

        for (int i = 0; i < 10; i++) {
            if (!keep_running) break;
            printf("[休息中] %02d / 10 秒\r", i + 1);
            fflush(stdout);
            safe_sleep(1);
        }
    
    }

    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, NULL, WNOHANG);
    }
    */
    return 0;
}
}

void create_fifo_if_need() {
    if (access(FIFO_NAME, F_OK) == -1) {
        if (mkfifo(FIFO_NAME, 0777) != 0) {
            perror("❌ 建立 FIFO 失敗");
            exit(EXIT_FAILURE);
        }
        printf("✅ 建立 FIFO: %s\n", FIFO_NAME);
    } else {
        chmod(FIFO_NAME, 0777);
    }
}

void signal_handler(int sig) {
    printf("\n🛑 收到 Ctrl+C，中斷中...\n");
    keep_running = 0;
    kill(-getpgrp(), SIGTERM); // 改成殺整個 process group
    _exit(130); // 強制直接結束 control 本身
}

void cleanup_on_exit() {
    printf("\n🧹 清理中...\n");

    if (mic_fd >= 0) {
        ioctl(mic_fd, 0, 0);
        close(mic_fd);
    }

    unlink(FIFO_NAME);
    printf("✅ 清理完成，退出\n");
}

void safe_sleep(int seconds) {
    struct timespec req = {seconds, 0};
    while (nanosleep(&req, &req) == -1 && errno == EINTR) {
        if (!keep_running) break;
    }
}