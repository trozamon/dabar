// TODO: add X lock notification so that keepassxc locks



// TODO: fork into background upon launch
// TODO: check for self before running




// TODO: move i3locker to separate binary and launch in i3/config
//void run_i3lock()
//{
//        pid_t res = fork();
//        char* i3lock_args[] = { "/usr/bin/i3lock", "--color", "000000", NULL };
//
//        if (res == 0)
//        {
//                int exists = check_proc_exists("i3lock");
//
//                if (!exists)
//                {
//                        //execve("/usr/bin/i3lock", i3lock_args, environ);
//                }
//
//                exit(0);
//        }
//}
//
//void lockdown()
//{
//        if (screen_locked)
//        {
//                return;
//        }
//
//        run_i3lock();
//        // TODO: add suorafx setting - red when locked
//
//        screen_locked = 1;
//}
//
//void unlockdown()
//{
//        screen_locked = 0;
//
//        // TODO: add suorafx setting - solid blue when active
//}

int main(void)
{
        return 0;
}
