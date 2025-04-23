#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sound/asoundlib.h>
#include "micdev.h"

#define DEVICE_PATH "/dev/inmp441_mic"
#define FIFO_PATH "/tmp/inmp_rec"

extern void set_mic_substream(const char *cardname, int device);

void write_wav_header(FILE *fp, int data_len, int sample_rate, short channels, short bits_per_sample) {
    int byte_rate = sample_rate * channels * bits_per_sample / 8;
    int block_align = channels * bits_per_sample / 8;

    fwrite("RIFF", 1, 4, fp);
    int chunk_size = 36 + data_len;
    fwrite(&chunk_size, 4, 1, fp);
    fwrite("WAVE", 1, 4, fp);

    fwrite("fmt ", 1, 4, fp);
    int subchunk1_size = 16;
    short audio_format = 1;
    fwrite(&subchunk1_size, 4, 1, fp);
    fwrite(&audio_format, 2, 1, fp);
    fwrite(&channels, 2, 1, fp);
    fwrite(&sample_rate, 4, 1, fp);
    fwrite(&byte_rate, 4, 1, fp);
    fwrite(&block_align, 2, 1, fp);
    fwrite(&bits_per_sample, 2, 1, fp);

    fwrite("data", 1, 4, fp);
    fwrite(&data_len, 4, 1, fp);
}

int main() {
    if (access(FIFO_PATH, F_OK) == -1) {
        mkfifo(FIFO_PATH, 0666);
    }

    int mic_fd = open(DEVICE_PATH, O_RDWR);
    if (mic_fd < 0) {
        perror("open char device");
        return 1;
    }

    snd_pcm_t *handle = NULL;
    char *buffer = NULL;
    char chunk[1024];
    FILE *fp = NULL;
    int recording = 0;
    int read_bytes = 0; 
    int ret;

    printf("[control] Waiting for echo 1 or 0 to /tmp/rec\n");
    int fifo_fd = open(FIFO_PATH, O_RDONLY);
    char cmd;

    while (read(fifo_fd, &cmd, 1) > 0) {
        if (cmd == '1' && !recording) {
            printf("[control] START\n");
            if (snd_pcm_open(&handle, "default", SND_PCM_STREAM_CAPTURE, 0) < 0) {
                fprintf(stderr, "Cannot open PCM device\n");
                continue;
            }

            if (snd_pcm_set_params(handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,1, 16000, 1, 500000) < 0) {
                fprintf(stderr, "Unable to set PCM parameters\n");
                snd_pcm_close(handle);
                continue;
            }

            set_mic_substream(NULL, 0);
            ioctl(mic_fd, IOC_MIC_START_RECORD, 0);
            fp = fopen("output.wav", "wb");
            write_wav_header(fp, read_bytes, 16000, 1, 16);
            recording = 1;

            // fork 出子程序，5 秒後自動送 "0" 到 fifo
            if (fork() == 0) {
                sleep(5);
                int tmp_fd = open(FIFO_PATH, O_WRONLY);
                if (tmp_fd >= 0) {
                    write(tmp_fd, "0", 1);
                    close(tmp_fd);
                }
                exit(0);
            }
        } else if (cmd == '0' && recording) {
            printf("[control] STOP\n");
            ioctl(mic_fd, IOC_MIC_STOP_RECORD, 0);
            while((ret = read(mic_fd, chunk, sizeof(chunk))) >0 ){
                fwrite(chunk,1,ret,fp);
                read_bytes += ret;
            }
            fseek(fp, 0, SEEK_SET);
            write_wav_header(fp, bytes_read, 16000, 1, 16);  // 寫回正確長度 header
            fclose(fp);

            if (handle) {
                snd_pcm_drain(handle);
                snd_pcm_close(handle);
                handle = NULL;
            }
            recording = 0;
        }
    }

    close(fifo_fd);
    close(mic_fd);
    return 0;
}
