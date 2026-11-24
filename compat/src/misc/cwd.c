/* get_current_dir_name(), the GNU extension liblinphone uses when $HOME is
   not set; declared in aos_compat.h. */
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

char *get_current_dir_name(void)
{
    for (size_t size = 256; size <= 65536; size *= 2) {
        char *buf = malloc(size);
        if (buf == NULL) {
            return NULL;
        }
        if (getcwd(buf, size) != NULL) {
            return buf;
        }
        int err = errno;
        free(buf);
        if (err != ERANGE) {
            errno = err;
            return NULL;
        }
    }
    errno = ERANGE;
    return NULL;
}
