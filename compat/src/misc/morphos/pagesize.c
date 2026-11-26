/*
 * getpagesize() for MorphOS, which libnix does not provide: the PowerPC
 * MMU page of MorphOS is 4 KB.
 */
#include <unistd.h>

int getpagesize(void) {
    return 4096;
}
