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

static char *empty = "";
static volatile int running = 1;
static int mem_fd = 0;

void nicely_exit(int sig)
{
        if (SIGINT == sig)
        {
                running = 0;
        }
}

char * get_mem(void)
{
        char *res = 0;
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
                        char *tmp;

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

char * get_time(void)
{
        char *res = calloc(128, sizeof(char));
        time_t t;
        struct tm timeval;

        t = time(NULL);
        struct tm *err = localtime_r(&t, &timeval);

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

char * dumb_read(const char *fname)
{
        int fd = open(fname, O_RDONLY);

        if (fd < 0)
        {
                return NULL;
        }

        char *content = malloc(3);
        ssize_t res = read(fd, content, 3);

        if (res <= 0)
        {
                free(content);
                return empty;
        }

        size_t i = 2;
        while (i > 0 && content[i] == '\n')
        {
                content[i] = 0;
        }

        return content;
}

char * get_battery(void)
{
        char **batteries = calloc(2, sizeof(char *));
        DIR *power_sup = opendir("/sys/class/power_supply");

        if (!power_sup)
        {
                return empty;
        }

        struct dirent *dir;
        size_t i = 0;

        while((dir = readdir(power_sup)) != NULL)
        {
                if (strncmp("BAT", dir->d_name, 3) == 0)
                {
                        batteries[i] = strdup(dir->d_name);
                        i++;
                }
        }

        closedir(power_sup);

        char *res = (char *) calloc(128, sizeof(char));
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
                char *cap = dumb_read(fname);
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

                strncpy((char *) (res + offset), tmp, 128 - offset);
                offset += added_len;
                free(cap);
                cap = NULL;
                j++;
        }

        return res;
}

int main(void)
{
        signal(SIGINT, nicely_exit);
        struct pollfd in;
        char *mem_res;
        char *time_res;
        char *bat_res;

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

                printf(",[");
                printf("{\"name\":\"mem\",\"full_text\":\"%s\"}", mem_res);
                printf(",{\"name\":\"time\",\"full_text\":\"%s\"}", time_res);
                printf(",{\"name\":\"bat\",\"full_text\":\"%s\"}", bat_res);
                printf("]\n");
                fflush(stdout);

                free(mem_res);
                mem_res = NULL;
                free(time_res);
                time_res = NULL;
                free(bat_res);
                bat_res = NULL;

                int err = poll(&in, 1, 5000);
                if (err == -1)
                {
                        running = 0;
                }
        }

        printf("]\n");

        return 0;
}
