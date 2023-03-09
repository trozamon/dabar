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
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/extensions/XInput2.h>

#include "dabar-common.h"

#define SOCKET_BACKLOG 5
#define DEFAULT_LOCK_TIME 300
#define POLL_DELAY 5000
#define BUF_SIZE 256

/* TODO: allow setting longer timeout for autolock, or locking at a specific time */
/* TODO: add a socket to communicate with the other process */
/* TODO: add suorafx setting - red when locked, blue when active */

struct pollset
{
        struct pollfd* pfds;
        size_t npfds;
};

static int xi_ext_opcode;
static Display* root_display;
static int active_screen;
static Window active_root;
static time_t last_active_time;

extern char** environ;
static volatile int screen_locked = 0;
static volatile int running = 1;
static volatile pid_t child_proc = 0;
static volatile time_t lock_time;

int dabar_x_init(void);
int dabar_x_close(void);

int open_display(void)
{
        root_display = XOpenDisplay(NULL);

        if (!root_display)
        {
                fprintf(stderr, "Failed to open display.\n");
                return 1;
        }

        active_screen = DefaultScreen(root_display);
        active_root = RootWindow(root_display, active_screen);

        return 0;
}

int xinput_extensions_init(void)
{
        int event;
        int error;

        int res = XQueryExtension(root_display, "XInputExtension",
                        &xi_ext_opcode, &event, &error);
        if (!res)
        {
                return 2;
        }

        int maj = 2;
        int min = 2;

        res = XIQueryVersion(root_display, &maj, &min);
        if (res == BadRequest)
        {
                return 3;
        }
        else if (res != Success)
        {
                return 4;
        }

        return 0;
}

int event_select_xi(void)
{
        XIEventMask masks[1];
        unsigned char mask[(XI_LASTEVENT + 7)/8];

        memset(mask, 0, sizeof(mask));
        XISetMask(mask, XI_RawMotion);
        XISetMask(mask, XI_RawButtonPress);
        XISetMask(mask, XI_RawTouchUpdate);
        XISetMask(mask, XI_RawKeyPress);

        masks[0].deviceid = XIAllMasterDevices;
        masks[0].mask_len = sizeof(mask);
        masks[0].mask = mask;

        XISelectEvents(root_display, active_root, masks, 1);
        XFlush(root_display);

        return 0;
}

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

int handle_message(struct pollfd* pfds, size_t idx, char* buf)
{
        int err = 0;
        char* tmp;
        time_t new_lock_time;

        switch (buf[0])
        {
                case DABAR_MSG_REQ_SET_LOCKTIME:
                        memcpy(((void*) &new_lock_time), buf + 1, sizeof(time_t));

                        if (new_lock_time > lock_time)
                        {
                                tmp = dabar_format_time(new_lock_time);
                                fprintf(stderr, "Updating lock time to %s\n", tmp);
                                lock_time = new_lock_time;
                                free(tmp);
                        }
                        /* FALLTHROUGH */

                case DABAR_MSG_REQ_LOCKTIME:
                        memset(buf, 0, BUF_SIZE);
                        buf[0] = DABAR_MSG_RESP_OK;
                        memcpy(buf + 1, (void*) &lock_time, sizeof(time_t));

                        err = write(pfds[idx].fd, buf, sizeof(time_t) + 1);
                        if (err == -1)
                        {
                                perror("write");
                                return -1;
                        }
                        break;

                default:
                        fprintf(stderr, "Read unknown message type\n");
                        return -2;
        }

        return 0;
}

int handle_read(struct pollfd* pfds, size_t idx)
{
        // TODO: allocate this outside somewhere (pollset?) and re-use
        char* buf;
        int err;

        buf = malloc(BUF_SIZE);
        memset(buf, 0, BUF_SIZE);

        err = read(pfds[idx].fd, buf, BUF_SIZE);

        if (err == -1)
        {
                perror("read");
                return err;
        }
        else if (err == 0)
        {
                err = close(pfds[idx].fd);

                pfds[idx].fd = ~pfds[idx].fd;

                if (err == -1)
                {
                        perror("close");
                        return err;
                }
        }
        else if (err <= BUF_SIZE)
        {
                err = handle_message(pfds, idx, buf);
        }

        free(buf);
        return err;
}

int handle_sockets(struct pollset* set, int listen_sock)
{
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
                        }
                }
                else if (pfds[i].revents & POLLIN)
                {
                        err = handle_read(pfds, i);

                        if (err)
                        {
                                return err;
                        }
                }
        }

        return 0;
}

void pollset_init(struct pollset* set)
{
        set->pfds = NULL;
        set->npfds = 0;
}

void lock_time_refresh(void)
{
        XEvent ev;
        int had_activity = 0;

        while (XPending(root_display) > 0)
        {
                had_activity = 1;

                XNextEvent(root_display, &ev);
                XFreeEventData(root_display, &ev.xcookie);
        }

        if (had_activity)
        {
                lock_time = time(NULL) + DEFAULT_LOCK_TIME;
        }
}

int dabar_x_close()
{
        if (root_display)
        {
                XCloseDisplay(root_display);
                root_display = NULL;
        }

        return 0;
}

int dabar_x_init()
{
        last_active_time = time(NULL);
        xi_ext_opcode = -1;
        int err = 0;

        err = open_display();
        if (err)
        {
                fprintf(stderr, "Failed to initialize X session: %d.\n", err);
                return err;
        }

        err = xinput_extensions_init();
        if (err)
        {
                fprintf(stderr, "Failed to initialize X extensions: %d.\n",
                                err);
                return err;
        }

        event_select_xi();

        return 0;
}

int main(void)
{
        struct pollset set;
        int err = 0;
        int sock = 0;

        pollset_init(&set);
        lock_time = time(NULL) + DEFAULT_LOCK_TIME;

        signal(SIGINT, nicely_exit);
        dabar_x_init();
        err = server_socket_init(&sock);

        if (err)
        {
                return err;
        }

        pollset_add_sock(&set, sock);

        while (running)
        {
                time_t now = time(NULL);
                int time_left;

                lock_time_refresh();
                time_left = lock_time - now;

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

        dabar_x_close();
        server_sock_deinit(&sock);

        return 0;
}
