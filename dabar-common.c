#include "dabar-common.h"

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

static const int LOCKING_THRESHOLD = 5 * 60;

static int xi_ext_opcode;
static Display* root_display;
static int active_screen;
static Window active_root;
static time_t last_active_time;

int dabar_get_lock_countdown()
{
        XEvent ev;
        int had_activity = 0;
        time_t now;
        int countdown = LOCKING_THRESHOLD;

        while (XPending(root_display) > 0)
        {
                had_activity = 1;
                XNextEvent(root_display, &ev);
                XFreeEventData(root_display, &ev.xcookie);
        }

        if (had_activity)
        {
                last_active_time = time(NULL);
        }

        now = time(NULL);
        countdown = LOCKING_THRESHOLD - ((int) (now - last_active_time));

        if (countdown < 0)
        {
                countdown = 0;
        }
        else if (countdown > LOCKING_THRESHOLD)
        {
                countdown = LOCKING_THRESHOLD;
        }

        return countdown;
}

int dabar_check_proc_exists(const char* proc_name)
{
        int exists = 0;
        struct dirent* ent;
        char* fname;
        int fd;
        DIR* proc;

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

static int open_display(void)
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

static int xinput_extensions_init(void)
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

static int event_select_xi(void)
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

int dabar_common_x_init()
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
