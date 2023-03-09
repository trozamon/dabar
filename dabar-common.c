#include "dabar-common.h"

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SOCKET_NAME ".dabar-autolock.sock"

int dabar_check_proc_exists(const char* proc_name)
{
        int exists = 0;
        struct dirent* ent;
        char* fname;
        int fd;
        DIR* proc;

        if (!proc_name)
        {
                return exists;
        }

        proc = opendir("/proc");

        if (!proc)
        {
                return exists;
        }

        fname = calloc(1024, sizeof(char));
        ent = readdir(proc);
        while (!exists && ent)
        {
                memset(fname, 0, 1024);
                int res = snprintf(fname, 1024,
                                "/proc/%s/cmdline", ent->d_name);

                fprintf(stderr, "Checking %s\n", fname);

                if (res <= 0)
                {
                        ent = readdir(proc);
                        continue;
                }

                fd = open(fname, O_RDONLY);
                if (fd < 0)
                {
                        ent = readdir(proc);
                        continue;
                }

                memset(fname, 0, 1024);
                res = read(fd, fname, 1024);
                if (res < 0)
                {
                        close(fd);
                        fd = -1;
                        ent = readdir(proc);
                        continue;
                }

                if (strstr(fname, proc_name))
                {
                        exists = 1;
                }

                if (fd >= 0)
                {
                        close(fd);
                        fd = -1;
                }

                ent = readdir(proc);
        }

        if (fd > 0)
        {
                close(fd);
        }

        closedir(proc);
        free(fname);

        return exists;
}

char* dabar_socket_name(void)
{
        char* tmp;
        char* home_dir;
        size_t len;

        home_dir = getenv("HOME");
        len = strlen(home_dir) + strlen(SOCKET_NAME) + 2;

        tmp = calloc(len, sizeof(char));
        memset(tmp, 0, len);
        snprintf(tmp, len, "%s/%s", home_dir, SOCKET_NAME);

        return tmp;
}

char* dabar_format_time(time_t t)
{
        char* res = calloc(128, sizeof(char));
        struct tm timeval;

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
