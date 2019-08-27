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

int main(void)
{
        signal(SIGINT, nicely_exit);
        struct pollfd in;
        char *mem_res;
        char *time_res;

        in.fd = STDIN_FILENO;
        in.events = POLLIN;

        printf("{ \"version\": 1 }\n");
        printf("[\n");
        printf("[]\n");

        while (running)
        {
                mem_res = get_mem();
                time_res = get_time();

                printf(",[");
                printf("{\"name\":\"mem\",\"full_text\":\"%s\"}", mem_res);
                printf(",{\"name\":\"time\",\"full_text\":\"%s\"}", time_res);
                printf("]\n");
                fflush(stdout);

                free(mem_res);
                mem_res = NULL;
                free(time_res);
                time_res = NULL;

                int err = poll(&in, 1, 5000);
                if (err == -1)
                {
                        running = 0;
                }
        }

        printf("]\n");

        return 0;
}
