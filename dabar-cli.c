#define _XOPEN_SOURCE

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "dabar-common.h"

int socket_init(int* sock)
{
        struct sockaddr_un name;
        struct stat sock_stat;
        char* sock_name = NULL;
        int err;

        sock_name = dabar_socket_name();
        if (sizeof(name.sun_path) <= strlen(sock_name))
        {
                fprintf(stderr, "Cannot fit socket name %s\n", sock_name);
                return 1;
        }

        err = stat(sock_name, &sock_stat);
        if (err && errno == ENOENT)
        {
                fprintf(stderr, "autolock not running\n");
                return -1;
        } else if (err)
        {
                perror("stat");
                return -1;
        }

        *sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (*sock == -1)
        {
                perror("socket");
                return -1;
        }

        memset(&name, 0, sizeof(name));

        name.sun_family = AF_UNIX;
        strncpy(name.sun_path, sock_name, sizeof(name.sun_path) - 1);

        err = connect(*sock, (const struct sockaddr*) &name, sizeof(name));
        if (err)
        {
                perror("connect");
                return -1;
        }

        return 0;
}

int print_locktime(int sock)
{
        char* tmp = malloc(256);
        int err;

        memset(tmp, 0, 256);
        tmp[0] = DABAR_MSG_REQ_LOCKTIME;

        err = write(sock, tmp, strlen(tmp));
        if (err == -1)
        {
                perror("write");
                return 1;
        }

        err = read(sock, tmp, 256);
        if (err == -1)
        {
                perror("read");
                return 1;
        }

        if (tmp[0] == DABAR_MSG_RESP_OK)
        {
                time_t t;
                char* msg;

                memcpy((void*) &t, tmp + 1, sizeof(time_t));
                msg = dabar_format_time(t);
                printf("%s\n", msg);
        }

        return 0;
}

int update_locktime(int sock, const char* cmd)
{
        char* tmp = malloc(256);
        char* res;
        time_t t;
        struct tm t_raw;
        int err;

        memset(tmp, 0, 256);
        tmp[0] = DABAR_MSG_REQ_SET_LOCKTIME;

        t = time(NULL);
        localtime_r(&t, &t_raw);

        if (strlen(cmd) != 5)
        {
                fprintf(stderr, "Could not parse time\n");
                return -1;
        }

        res = strptime(cmd, "%H:%M", &t_raw);
        if (res == NULL)
        {
                fprintf(stderr, "strptime: could not parse time\n");
                return -1;
        }

        t_raw.tm_sec = 0;
        t = mktime(&t_raw);
        memcpy(tmp + 1, (void*) &t, sizeof(time_t));

        err = write(sock, tmp, sizeof(time_t) + 1);
        if (err == -1)
        {
                perror("write");
                return -1;
        }

        err = read(sock, tmp, 256);
        if (err == -1)
        {
                perror("read");
                return -1;
        }

        if (tmp[0] == DABAR_MSG_RESP_OK)
        {
                time_t t;
                char* msg;

                memcpy((void*) &t, tmp + 1, sizeof(time_t));
                msg = dabar_format_time(t);
                printf("Set to %s\n", msg);
        }
        else
        {
                printf("error, read %d bytes\n", err);
        }

        return 0;
}

int main(int argc, char** argv)
{
        int sock;
        int err = 0;

        err = socket_init(&sock);
        if (err)
        {
                return err;
        }

        if (argc == 1)
        {
                print_locktime(sock);
        }
        else if (strlen(argv[1]) == 5)
        {
                update_locktime(sock, argv[1]);
        }
        else
        {
                printf("usage: dabar-cli [hh:mm]\n");
        }

        err = close(sock);
        if (err == -1)
        {
                perror("close");
                return 1;
        }

        return 0;
}
