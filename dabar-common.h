#ifndef DABAR_COMMON_H
#define DABAR_COMMON_H

#include <time.h>

/* Message format: type|data */
#define DABAR_MSG_RESP_OK 0x01 /* data: time_t of current lock time */
#define DABAR_MSG_REQ_LOCKTIME 0x02 /* data: none */
#define DABAR_MSG_REQ_SET_LOCKTIME 0x03 /* data: time_t of desired lock time */
#define DABAR_MSG_REQ_INCR_LOCKTIME 0x04 /* data: string to add - +m for minute add, +h:m for hour and minute add */

/**
 * Check if a process exists.
 */
int dabar_check_proc_exists(const char* proc_name);

/**
 * Get the socket to use for communicating with autolock
 */
char* dabar_socket_name(void);

/**
 * Format the given time_t.
 *
 * Caller is responsible for freeing the returned pointer.
 */
char* dabar_format_time(time_t t);

#endif /* DABAR_COMMON_H */
