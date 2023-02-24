#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dabar-common.h"

int main(void)
{
        struct sockaddr_un name;
        struct stat sock_stat;
        char* sock_name = NULL;
        int sock;
        int err = 0;

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
                return 1;
        } else if (err)
        {
                perror("stat");
                return 1;
        }

        sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (sock == -1)
        {
                perror("socket");
                return 1;
        }

        memset(&name, 0, sizeof(name));

        name.sun_family = AF_UNIX;
        strncpy(name.sun_path, sock_name, sizeof(name.sun_path) - 1);

        err = connect(sock, (const struct sockaddr*) &name, sizeof(name));
        if (err)
        {
                perror("connect");
                return 1;
        }

        char* tmp = "does it work";

        err = write(sock, tmp, strlen(tmp));
        if (err == -1)
        {
                perror("write");
                return 1;
        }

        err = close(sock);
        if (err == -1)
        {
                perror("close");
                return 1;
        }

        return 0;
}
