#ifndef DABAR_COMMON_H
#define DABAR_COMMON_H

/**
 * Initialize dabar common.
 */
int dabar_common_x_init(void);

/**
 * Get the remaining time before locking.
 */
int dabar_get_lock_countdown(void);

/**
 * Check if a process exists.
 */
int dabar_check_proc_exists(const char* proc_name);

#endif /* DABAR_COMMON_H */
