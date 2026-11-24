/* getpriority() and setpriority(); see sys/resource.h for the reasons. */
#include <sys/resource.h>

#include <errno.h>

int getpriority(int which, id_t who)
{
    (void)which;
    (void)who;
    errno = 0;
    return 0;
}

int setpriority(int which, id_t who, int prio)
{
    (void)which;
    (void)who;
    (void)prio;
    errno = EPERM;
    return -1;
}
