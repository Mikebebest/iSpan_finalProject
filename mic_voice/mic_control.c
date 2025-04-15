#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "micdev.h"
#include <sys/types.h>
#include <sys/stat.h>

#define BUF_LEN 4096
#define PCM_DEVICE "/dev/snd/pcmC0D0c"
#define MIC_DEVICE "/dev/inmp441_mic"
#define FIFO_NAME "/tmp/rec_fifo"

//char rec_buf[BUF_LEN];
int bg_record_running = 1;
int user_cmd = 0;  //user 可能傳送的指令
int open_mode = O_WRONLY;
void *bg_record_monitor(void *arg);
void *manual_record(void *arg);

int main(){


    int res;
    pthread_t bg_thread;
    pthread_t man_thread;
    void *thread_result;

    int mic_fd = open(MIC_DEVICE, O_RDWR);
    if(mic_fd < 0){
        printf("無法開啟麥克風");
        exit(-1);
    }
    if(access(FIFO_NAME,F_OK) == -1){
        res = mkfifo(FIFO_NAME, 0777);
        if(res != 0){
            fprintf(stderr, "無法建立fifo %s\n",FIFO_NAME);
            exit(EXIT_FAILURE);
        }
    }
    
    //開始背景的錄音程序
    res = pthread_create(&bg_thread, NULL, bg_record_monitor, NULL);
    if(res != 0){
        perror("背景錄音程序執行失敗!");
        exit(EXIT_FAILURE);
    }
    pthread_detach(bg_thread);

    if(user_cmd == 1){
        res = pthread_create(&man_thread, NULL, manual_record, NULL);
        if(res != 0){
            perror("手動錄音程序失敗!");
            exit(EXIT_FAILURE);
        }
    }
    if(user_cmd == 2){
        if((res = pthread_cancel(man_thread)) != 0){
            perror("手動結束錄音失敗!");
            exit(EXIT_FAILURE);
        };
    }
    res = pthread_join(man_thread, &thread_result);
    if(res != 0){
        perror("執行緒join failed");
        exit(EXIT_FAILURE);
    }
    close(mic_fd);
    exit(EXIT_SUCCESS);    
}

void *bg_record_monitor(void *arg){
    int res;
    int mic_fifo = open(FIFO_NAME,open_mode);
    if(mic_fifo == -1){
        perror("Fifo 開啟失敗");
        exit(EXIT_FAILURE);
    }
    char mic_buf[BUF_LEN];
    if((res = ioctl(mic_fd,IOC_MIC_START_RECORD,NULL)) <0){
        printf("ioctl mic 自動監聽開啟失敗!"); exit(-1);
    };
    int fd = open(PCM_DEVICE, O_RDONLY);
    if(fd < 0){
        perror("開啟PCM_DEVICE failed!");
        return NULL;
    }
    while(bg_record_running){    
        int voice = read(fd, mic_buf, sizeof(mic_buf));  //從I2S 讀取音訊資料
        if(voice > 0){
            res = write(mic_fifo,mic_buf,sizeof(mic_buf));
        }
    }

    close(fd);
    close(mic_fifo);
    if((res = ioctl(mic_fd,IOC_MIC_STOP_RECORD,NULL)) < 0){
        printf("ioctl mic 自動監聽關閉失敗!"); exit(-1);
    }
    pthread_exit(0);
}

void *manual_record(void *arg){
    char mic_buf[BUF_LEN];
    int fd = open(PCM_DEVICE, O_RDONLY);
    if(fd < 0){
        perror("開啟PCM_DEVICE failed!");
        exit(EXIT_FAILURE);
    }
    int mic_fifo = open(FIFO_NAME, open_mode);
    if(mic_fifo == -1){
        perror("Fifo 開啟失敗!");
        exit(EXIT_FAILURE);
    }
    int res;
    res = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    if(res != 0){
        perror("Thread 設定失敗");
        exit(EXIT_FAILURE);
    }
    res = pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    if(res != 0){
        perror("Thread 設定cancel type失敗");
        exit(EXIT_FAILURE);
    }
    if(fd < 0){
        perror("開啟PCM_DEVICE failed!");
        return NULL;
    }
    bg_record_running = 0;
    int record_ms = 0;

    if((res = ioctl(mic_fd,IOC_MIC_START_RECORD,NULL)) <0){
        printf("ioctl mic 手動錄音開啟失敗!"); exit(-1);
    };
    while (record_ms < 5000){

        int voice = read(fd, mic_buf, sizeof(mic_buf));   //從I2S 讀取音訊資料
        if (voice > 0) {     //搬移資料去FIFO 做指令辨識
                write(mic_fifo,mic_buf,sizeof(mic_buf));
        }
        pthread_testcancel();  // 可中斷點
        usleep(100000);        // 讀取間隔 100ms
        record_ms += 100;
    }

    close(fd);
    close(mic_fifo);
    bg_record_running = 1;
    if((res = ioctl(mic_fd,IOC_MIC_STOP_RECORD,NULL)) < 0){
        printf("ioctl mic 手動錄音關閉失敗!"); exit(-1);
    }
    pthread_exit(0);
}