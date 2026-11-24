/*
 * sys/resource.h - adds the process priority calls to the posixc header,
 * which declares them as not implemented. libaoscompat provides them.
 *
 * Exec task priorities are not nice values, and raising one starves every
 * task below it, so setpriority() refuses with EPERM. Threads change their
 * Exec priority through pthread_setschedparam(), whose range on AROS is
 * sched_get_priority_min() to sched_get_priority_max() (-2 to 2).
 */
#ifndef AOS_SYS_RESOURCE_H
#define AOS_SYS_RESOURCE_H

/* struct rlimit, getrlimit(), getrusage() and the rest come from posixc. */
#include_next <sys/resource.h>

#include <sys/types.h>

#ifndef PRIO_PROCESS
#define PRIO_PROCESS 0
#define PRIO_PGRP    1
#define PRIO_USER    2
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Nice value of the caller. @return 0 (the default nice value). */
int getpriority(int which, id_t who);

/** @brief Refuses to change priorities. @return -1 with errno EPERM. */
int setpriority(int which, id_t who, int prio);

#ifdef __cplusplus
}
#endif

#endif /* AOS_SYS_RESOURCE_H */
