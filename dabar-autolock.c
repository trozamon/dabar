#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include "dabar-common.h"

#define SOCKET_BACKLOG 5
#define DEFAULT_LOCK_TIME 300
#define POLL_DELAY 5000
#define BUF_SIZE 256

struct pollset
{
        struct pollfd* pfds;
        size_t npfds;
};

extern char** environ;
static volatile int screen_locked = 0;
static volatile int running = 1;
static volatile pid_t child_proc = 0;

/* TODO: allow setting longer timeout for autolock, or locking at a specific time */
/* TODO: add a socket to communicate with the other process */
/* TODO: add suorafx setting - red when locked, blue when active */

void run_i3lock()
{
        pid_t res;
        char* i3lock_args[] = { "/usr/bin/i3lock", "--color", "000000", NULL };
        int exists = dabar_check_proc_exists("i3lock");

        if (exists)
        {
                return;
        }

        res = fork();

        if (res == 0)
        {
                execve("/usr/bin/i3lock", i3lock_args, environ);
        }

        child_proc = res;
}

void lockdown()
{
        if (screen_locked)
        {
                return;
        }

        run_i3lock();

        screen_locked = 1;
}

void unlockdown()
{
        screen_locked = 0;
}

void nicely_exit(int sig)
{
        if (SIGINT == sig)
        {
                running = 0;
        }
}

int server_socket_init(int* res)
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
                err = -1;
                goto ssi_return;
        }

        err = stat(sock_name, &sock_stat);
        if (err && errno != ENOENT)
        {
                perror("stat");
                goto ssi_return;
        }
        else if (!err)
        {
                fprintf(stderr, "autolock is already running\n");
                err = -1;
                goto ssi_return;
        }

        sock = socket(AF_UNIX, SOCK_SEQPACKET, 0);
        if (sock == -1)
        {
                perror("socket");
                err = -1;
                goto ssi_return;
        }

        memset(&name, 0, sizeof(name));

        name.sun_family = AF_UNIX;
        strncpy(name.sun_path, sock_name, sizeof(name.sun_path) - 1);

        err = bind(sock, (const struct sockaddr*) &name, sizeof(name));
        if (err)
        {
                perror("bind");
                goto ssi_return;
        }

        err = listen(sock, SOCKET_BACKLOG);
        if (err)
        {
                perror("listen");
                goto ssi_return;
        }

        *res = sock;

ssi_return:
        if (sock_name != NULL)
        {
                free(sock_name);
        }

        return err;
}

int server_sock_deinit(int* res)
{
        char* sock_name = NULL;
        int err = 0;

        err = close(*res);
        if (err)
        {
                perror("close");
                return err;
        }

        sock_name = dabar_socket_name();
        err = unlink(sock_name);
        if (err)
        {
                perror("unlink");
                return err;
        }

        free(sock_name);
        return 0;
}

void pollset_set_events(struct pollset* set)
{
        for (size_t i = 0; i < set->npfds; ++i)
        {
                set->pfds[i].events = POLLIN;
        }
}

void pollset_add_sock(struct pollset* set, int socket)
{
        /* TODO: search for a negative fd and re-use it */
        int idx = -1;

        if (!set->pfds)
        {
                set->pfds = (struct pollfd*) malloc(sizeof(struct pollfd));
                set->npfds = 1;
                idx = 0;
        }

        for (size_t i = 0; idx < 0 && i < set->npfds; i++)
        {
                if (set->pfds[i].fd < 0)
                {
                        idx = i;
                }
        }

        if (idx < 0)
        {
                idx = set->npfds;
                set->npfds++;
                set->pfds = (struct pollfd*) reallocarray(set->pfds, set->npfds, sizeof(struct pollfd));
        }

        set->pfds[idx].fd = socket;
        set->pfds[idx].events = POLLIN;
}

int handle_sockets(struct pollset* set, int listen_sock)
{
        char* buf;
        int err;
        struct pollfd* pfds = set->pfds;

        for (size_t i = 0; i < set->npfds; ++i)
        {
                if ((pfds[i].revents & POLLIN) && pfds[i].fd == listen_sock)
                {
                        err = accept(pfds[i].fd, NULL, NULL);
                        if (err == -1)
                        {
                                perror("accept");
                                return err;
                        }
                        else
                        {
                                pollset_add_sock(set, err);
                                fprintf(stderr, "Accepted\n");
                        }
                }
                else if (pfds[i].revents & POLLIN)
                {
                        buf = malloc(BUF_SIZE);
                        memset(buf, 0, BUF_SIZE);
                        err = read(pfds[i].fd, buf, 256);
                        fprintf(stderr, "Read %d: %s\n", err, buf);
                        free(buf);
                        if (err == -1)
                        {
                                perror("read");
                                return err;
                        }
                        else if (err == 0)
                        {
                                err = close(pfds[i].fd);
                                pfds[i].fd = ~pfds[i].fd;
                                if (err == -1)
                                {
                                        perror("close");
                                        return err;
                                }
                        }
                }
        }

        return 0;
}

int main(void)
{
        struct pollset set;
        int err = 0;
        int sock = 0;

        set.pfds = NULL;

        signal(SIGINT, nicely_exit);
        dabar_common_x_init();
        err = server_socket_init(&sock);

        if (err)
        {
                return err;
        }

        pollset_add_sock(&set, sock);

        while (running)
        {
                // TODO fix
                int time_left = dabar_get_lock_countdown();

                /* fprintf(stderr, "%d seconds left in countdown\n", time_left); */

                if (!screen_locked && time_left == 0)
                {
                        lockdown();
                }
                else if (screen_locked)
                {
                        unlockdown();
                }

                if (child_proc > 0)
                {
                        waitpid(child_proc, &err, WNOHANG);

                        if (WIFEXITED(err) || WIFSIGNALED(err))
                        {
                                child_proc = 0;
                        }
                }

                pollset_set_events(&set);
                err = poll(set.pfds, set.npfds, POLL_DELAY);
                if (err == -1)
                {
                        running = 0;
                }
                else if (err > 0)
                {
                        handle_sockets(&set, sock);
                }
        }

        dabar_common_x_close();
        server_sock_deinit(&sock);

        return 0;
}
