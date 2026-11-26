/* getpriority() and setpriority(); see sys/resource.h for the reasons. */
#include <sys/resource.h>

#include <errno.h>

/* libnix declares the "who" argument as int. */
#ifdef __MORPHOS__
typedef int aos_prio_who_t;
#else
typedef id_t aos_prio_who_t;
#endif

int getpriority(int which, aos_prio_who_t who) {
    (void)which;
    (void)who;
    errno = 0;
    return 0;
}

int setpriority(int which, aos_prio_who_t who, int prio) {
    (void)which;
    (void)who;
    (void)prio;
    errno = EPERM;
    return -1;
}
