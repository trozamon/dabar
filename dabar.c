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

extern char** environ;

static char* empty = "";
static volatile int running = 1;
static volatile int screen_locked = 0;
static int mem_fd = 0;

static int xi_ext_opcode = -1;
static Display* root_display;
static int active_screen;
static Window active_root;

static const int FIFTEEN_MINUTES = 15 * 60;
static time_t last_active_time;

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

        if (!power_sup)
        {
                return strdup(empty);
        }

        struct dirent* dir;
        size_t i = 0;

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

int get_lock_countdown()
{
        XEvent ev;
        int had_activity = 0;
        time_t now;
        int countdown = FIFTEEN_MINUTES;

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
        countdown = FIFTEEN_MINUTES - ((int) (now - last_active_time));

        if (countdown < 0)
        {
                countdown = 0;
        }
        else if (countdown > FIFTEEN_MINUTES)
        {
                countdown = FIFTEEN_MINUTES;
        }

        return countdown;
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

int check_proc_exists(const char* proc_name)
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

// TODO: add ip addr
// TODO: add X lock notification so that keepassxc locks

void run_i3lock()
{
        pid_t res = fork();
        char* i3lock_args[] = { "/usr/bin/i3lock", "--color", "000000", NULL };

        if (res == 0)
        {
                int exists = check_proc_exists("i3lock");

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
        // TODO: add suorafx setting - breathing when locked

        screen_locked = 1;
}

void unlockdown()
{
        screen_locked = 0;

        // TODO: add suorafx setting - solid blue when active
}

int main(void)
{
        struct pollfd in;
        char* mem_res;
        char* time_res;
        char* bat_res;
        char* lock_res;
        int err;

        signal(SIGINT, nicely_exit);

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

        last_active_time = time(NULL);

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
                int time_left = get_lock_countdown();
                lock_res = fmt_lock_countdown_str(time_left);

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

                if (time_left == 0)
                {
                        lockdown();
                }

                int err = poll(&in, 1, 5000);
                if (err == -1)
                {
                        running = 0;
                }
        }

        printf("]\n");

        return 0;
}
