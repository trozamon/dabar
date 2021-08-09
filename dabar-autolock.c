#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "dabar-common.h"

extern char** environ;
static volatile int screen_locked = 0;
static volatile int running = 1;

/* TODO: add X lock notification so that keepassxc locks */
/* TODO: add a socket to communicate with the other process */
/* TODO: add suorafx setting - red when locked, blue when active */

void run_i3lock()
{
        pid_t res = fork();
        char* i3lock_args[] = { "/usr/bin/i3lock", "--color", "000000", NULL };

        if (res == 0)
        {
                int exists = dabar_check_proc_exists("i3lock");

                if (!exists)
                {
                        execve("/usr/bin/i3lock", i3lock_args, environ);
                }

                exit(0);
        }
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

int main(void)
{
        struct pollfd in;
        int err = 0;

        signal(SIGINT, nicely_exit);
        dabar_common_x_init();

        in.fd = STDIN_FILENO;
        in.events = POLLIN;

        while (running)
        {
                int time_left = dabar_get_lock_countdown();

                if (!screen_locked && time_left == 0)
                {
                        lockdown();
                }
                else if (screen_locked)
                {
                        unlockdown();
                }

                err = poll(&in, 1, 5000);
                if (err == -1)
                {
                        running = 0;
                }
        }

        return 0;
}
