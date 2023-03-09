#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include "dabar-common.h"

static char* empty = "";
static volatile int running = 1;
static int mem_fd = 0;

void nicely_exit(int sig)
{
        if (SIGINT == sig)
        {
                running = 0;
        }
}

char* get_mem(void)
{
        char* res = 0;
        char buf[1024];
        ssize_t err;

        if (!mem_fd)
        {
                mem_fd = open("/proc/meminfo", O_RDONLY);
        }
        else
        {
                lseek(mem_fd, 0, SEEK_SET);
        }

        err = read(mem_fd, buf, sizeof(buf));
        if (-1 == err)
        {
                res = calloc(15, sizeof(char));
                strcpy(res, "can't read mem");
        }
        else
        {
                // get numbers in kB from first and third lines
                size_t i = 0;
                unsigned long total = 0;
                unsigned long avail = 0;
                size_t lines = 0;

                while (i < sizeof(buf) && lines < 3)
                {
                        char* tmp;

                        if (buf[i] > '0' && buf[i] <= '9')
                        {
                                if (lines == 2)
                                {
                                        avail = strtoul(buf + i, &tmp, 10);
                                        break;
                                }
                                else if (lines == 0)
                                {
                                        total = strtoul(buf + i, &tmp, 10);
                                        i = tmp - buf;
                                }
                        }
                        else if (buf[i] == '\n')
                        {
                                lines++;
                        }

                        i++;
                }

                total /= 1024;
                avail /= 1024;

                res = calloc(256, sizeof(char));
                snprintf(res, 256, "%luMB / %luMB", total - avail, total);
        }

        return res;
}

/* TODO: remove */
char* get_time(void)
{
        char* res = calloc(128, sizeof(char));
        time_t t;
        struct tm timeval;

        t = time(NULL);
        struct tm* err = localtime_r(&t, &timeval);

        if (NULL == err)
        {
                res[0] = 0;
        }
        else
        {
                strftime(res, 128, "%F %T %Z (%z)", &timeval);
        }

        return res;
}

char* dumb_read(const char* fname)
{
        size_t LEN = 4;
        int fd = open(fname, O_RDONLY);

        if (fd < 0)
        {
                return NULL;
        }

        char* content = calloc(LEN, sizeof(char));
        ssize_t res = read(fd, content, LEN);
        close(fd);

        if (res <= 0)
        {
                free(content);
                return strdup(empty);
        }

        size_t i = LEN - 1;
        while (i > 0 && (content[i] < '0' || content[i] > '9'))
        {
                content[i] = 0;
                i--;
        }

        return content;
}

char* get_battery(void)
{
        char** batteries = calloc(2, sizeof(char*));
        DIR* power_sup = opendir("/sys/class/power_supply");
        struct dirent* dir;
        size_t i = 0;

        if (!power_sup)
        {
                free(batteries);
                return strdup(empty);
        }

        batteries[0] = 0;
        batteries[1] = 0;

        while((dir = readdir(power_sup)) != NULL)
        {
                if (strncmp("BAT", dir->d_name, 3) == 0)
                {
                        batteries[i] = strdup(dir->d_name);
                        i++;
                }
        }

        closedir(power_sup);

        char* res = (char*) calloc(128, sizeof(char));
        memset(res, 0, 128);

        size_t j = 0;
        size_t offset = 0;
        while (j < i)
        {
                char tmp[128];
                char fname[1024];
                int added_len;

                memset(tmp, 0, 128);
                sprintf(fname, "/sys/class/power_supply/%s/capacity",
                                batteries[j]);
                char* cap = dumb_read(fname);
                if (!cap)
                {
                        j++;
                        continue;
                }

                if (j == i - 1)
                {
                        added_len = snprintf(tmp, 128, "%s: %s%%",
                                        batteries[j], cap);
                }
                else
                {
                        added_len = snprintf(tmp, 128, "%s: %s%% | ",
                                        batteries[j], cap);
                }

                strncpy((char*) (res + offset), tmp, 128 - offset);
                offset += added_len;
                free(cap);
                cap = NULL;
                j++;
        }

        for (size_t i = 0; i < 2; i++)
        {
                if (batteries[i])
                {
                        free(batteries[i]);
                        batteries[i] = NULL;
                }
        }

        free(batteries);
        batteries = NULL;

        return res;
}

char* fmt_lock_countdown_str(int counter)
{
        char* res = NULL;
        int minutes;
        int seconds;

        minutes = counter / 60;
        seconds = counter % 60;

        res = calloc(6, sizeof(char));
        snprintf(res, 6, "%02d:%02d", minutes, seconds);

        return res;
}

// TODO: add ip addr

int main(void)
{
        struct pollfd in;
        char* mem_res;
        char* time_res;
        char* bat_res;
        char* lock_res;
        int err = 0;

        signal(SIGINT, nicely_exit);

        in.fd = STDIN_FILENO;
        in.events = POLLIN;

        printf("{ \"version\": 1 }\n");
        printf("[\n");
        printf("[]\n");

        while (running)
        {
                mem_res = get_mem();
                time_res = get_time();
                bat_res = get_battery();
                int time_left = 300; /* TODO: dabar_get_lock_countdown(); */
                lock_res = fmt_lock_countdown_str(time_left); // TODO replace

                printf(",[");
                printf("{\"name\":\"lock\",\"full_text\":\"%s\"},", lock_res);
                if (bat_res && strlen(bat_res) > 0)
                {
                        printf("{\"name\":\"bat\",\"full_text\":\"%s\"},", bat_res);
                }
                else
                {
                        printf("{\"name\":\"bat\",\"full_text\":\"no battery\"},");
                }
                printf("{\"name\":\"mem\",\"full_text\":\"%s\"}", mem_res);
                printf(",{\"name\":\"time\",\"full_text\":\"%s\"}", time_res);
                printf("]\n");
                fflush(stdout);

                free(mem_res);
                mem_res = NULL;
                free(time_res);
                time_res = NULL;
                free(bat_res);
                bat_res = NULL;
                free(lock_res);
                lock_res = NULL;

                err = poll(&in, 1, 5000);
                if (err == -1)
                {
                        running = 0;
                }
        }

        printf("]\n");

        return 0;
}
