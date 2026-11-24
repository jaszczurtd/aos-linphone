/*
 * getpagesize() for AROS, which posixc does not provide.
 */
#include <unistd.h>

int getpagesize(void)
{
    long size = sysconf(_SC_PAGESIZE);
    return size > 0 ? (int)size : 4096;
}
