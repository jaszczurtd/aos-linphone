/*
 * select() and poll() over WaitSelect().
 *
 * WaitSelect() waits on sockets of the shared base only. A descriptor
 * below the base's descriptor table size that is not one of its sockets (a
 * posixc file or pipe) makes it fail with EBADF; a higher one is ignored
 * silently, since the stack clamps nfds to that size. posixc has a select()
 * stub that always returns 0 and no poll(); these definitions replace the
 * stub at link time.
 */
#include "net_internal.h"

#include <bsdsocket/socketbasetags.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Without bsdsocket.library, a wait on no descriptors still sleeps, so that
   select(0, NULL, NULL, NULL, &tv) keeps working as a sleep (ortp's timer
   uses it this way). Returns 0, or -1 with ENETDOWN when there is no
   timeout to sleep for. */
static int aos_wait_without_base(const struct timeval *tv)
{
    if (tv == NULL) {
        errno = aos_net_open_error;
        return -1;
    }
    struct timespec ts;
    ts.tv_sec = tv->tv_sec;
    ts.tv_nsec = (long)tv->tv_usec * 1000L;
    nanosleep(&ts, NULL);
    return 0;
}

static int aos_select_has_fds(int nfds, const fd_set *r, const fd_set *w, const fd_set *e)
{
    for (int fd = 0; fd < nfds; ++fd) {
        if ((r != NULL && FD_ISSET(fd, r)) || (w != NULL && FD_ISSET(fd, w)) ||
            (e != NULL && FD_ISSET(fd, e))) {
            return 1;
        }
    }
    return 0;
}

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    if (SocketBase == NULL) {
        if (aos_select_has_fds(nfds, readfds, writefds, exceptfds)) {
            errno = aos_net_open_error;
            return -1;
        }
        return aos_wait_without_base(timeout);
    }
    return __WaitSelect_WB(SocketBase, nfds, readfds, writefds, exceptfds, timeout, NULL);
}

/* Descriptors covered by the descriptor sets on the stack; poll() allocates
   larger sets for higher descriptor numbers. */
#define AOS_POLL_STACK_FDS 1024
#define AOS_POLL_STACK_WORDS (AOS_POLL_STACK_FDS / NFDBITS)

#define AOS_POLL_READ   (POLLIN | POLLRDNORM)
#define AOS_POLL_WRITE  (POLLOUT | POLLWRNORM | POLLWRBAND)
#define AOS_POLL_EXCEPT (POLLPRI | POLLRDBAND)

static fd_mask aos_poll_bit(int fd)
{
    return (fd_mask)(1u << ((unsigned int)fd % NFDBITS));
}

static void aos_poll_mark(fd_mask *set, int fd)
{
    set[fd / NFDBITS] |= aos_poll_bit(fd);
}

static int aos_poll_test(const fd_mask *set, int fd)
{
    return (set[fd / NFDBITS] & aos_poll_bit(fd)) != 0;
}

/* Size of the shared base's descriptor table: every socket of the base has
   a lower number. Returns -1 when the stack does not report it. */
static long aos_poll_table_size(void)
{
    struct TagItem tags[] = {
        {SBTM_GETVAL(SBTC_DTABLESIZE), 0},
        {TAG_DONE, 0},
    };
    if (__SocketBaseTagList_WB(SocketBase, tags) != 0) {
        return -1;
    }
    return (long)tags[0].ti_Data;
}

/* Whether fd is a socket of the shared base. */
static int aos_poll_is_socket(int fd)
{
    int type;
    socklen_t len = sizeof(type);
    return __getsockopt_WB(SocketBase, fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0;
}

/* Fills the three sets from fds, skipping entries already marked POLLNVAL.
   Returns the highest descriptor put in a set, or -1. */
static int aos_poll_fill(const struct pollfd *fds, nfds_t nfds, fd_mask *sets, size_t words)
{
    fd_mask *rd = sets, *wr = sets + words, *ex = sets + 2 * words;
    int top = -1;

    memset(sets, 0, 3 * words * sizeof(fd_mask));
    for (nfds_t i = 0; i < nfds; ++i) {
        int fd = fds[i].fd;
        if (fd < 0 || (fds[i].revents & POLLNVAL)) {
            continue;
        }
        if (fds[i].events & AOS_POLL_READ) {
            aos_poll_mark(rd, fd);
        }
        if (fds[i].events & AOS_POLL_WRITE) {
            aos_poll_mark(wr, fd);
        }
        if (fds[i].events & AOS_POLL_EXCEPT) {
            aos_poll_mark(ex, fd);
        }
        if (fd > top) {
            top = fd;
        }
    }
    return top;
}

/* Marks entries that are not sockets of the base as POLLNVAL. With
   table_size >= 0, every descriptor at or above it is marked without asking
   the stack. Returns the number of entries marked by this call. */
static int aos_poll_mark_invalid(struct pollfd *fds, nfds_t nfds, long table_size)
{
    int marked = 0;
    for (nfds_t i = 0; i < nfds; ++i) {
        int fd = fds[i].fd;
        if (fd < 0 || (fds[i].revents & POLLNVAL)) {
            continue;
        }
        int invalid = table_size >= 0 ? fd >= table_size : !aos_poll_is_socket(fd);
        if (invalid) {
            fds[i].revents = POLLNVAL;
            ++marked;
        }
    }
    return marked;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (nfds > 0 && fds == NULL) {
        errno = EFAULT;
        return -1;
    }

    int maxfd = -1;
    for (nfds_t i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
        if (fds[i].fd > maxfd) {
            maxfd = fds[i].fd;
        }
    }

    struct timeval tv;
    struct timeval *tvp = NULL;
    if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        tvp = &tv;
    }

    if (SocketBase == NULL) {
        if (maxfd >= 0) {
            errno = aos_net_open_error;
            return -1;
        }
        return aos_wait_without_base(tvp);
    }

    /* Descriptors the stack would ignore are invalid; with any invalid
       entry, poll() returns at once, as POSIX describes. */
    int invalid = maxfd >= 0 ? aos_poll_mark_invalid(fds, nfds, aos_poll_table_size()) : 0;
    if (invalid > 0) {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        tvp = &tv;
    }

    size_t words = maxfd < 0 ? 1 : (size_t)maxfd / NFDBITS + 1;
    fd_mask stack_sets[3 * AOS_POLL_STACK_WORDS];
    fd_mask *sets = stack_sets;
    if (words > AOS_POLL_STACK_WORDS) {
        sets = calloc(3 * words, sizeof(fd_mask));
        if (sets == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }

    int rc;
    for (;;) {
        /* WaitSelect() reads (nfds + NFDBITS - 1) / NFDBITS words of each
           set, so sets longer than an fd_set are fine. */
        int top = aos_poll_fill(fds, nfds, sets, words);
        rc = __WaitSelect_WB(SocketBase, top + 1, (fd_set *)sets, (fd_set *)(sets + words),
                             (fd_set *)(sets + 2 * words), tvp, NULL);
        if (rc >= 0 || errno != EBADF) {
            break;
        }
        /* A descriptor that is not a socket, or a socket another thread
           has just closed: mark it POLLNVAL and look at the rest again
           without waiting. Each round marks at least one entry. */
        int marked = aos_poll_mark_invalid(fds, nfds, -1);
        if (marked == 0) {
            errno = EBADF;
            break;
        }
        invalid += marked;
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        tvp = &tv;
    }

    int ready = -1;
    if (rc >= 0) {
        const fd_mask *rd = sets, *wr = sets + words, *ex = sets + 2 * words;
        ready = invalid;
        for (nfds_t i = 0; i < nfds && rc > 0; ++i) {
            int fd = fds[i].fd;
            if (fd < 0 || (fds[i].revents & POLLNVAL)) {
                continue;
            }
            short revents = 0;
            if (aos_poll_test(rd, fd)) {
                revents |= fds[i].events & AOS_POLL_READ;
            }
            if (aos_poll_test(wr, fd)) {
                revents |= fds[i].events & AOS_POLL_WRITE;
            }
            if (aos_poll_test(ex, fd)) {
                revents |= fds[i].events & AOS_POLL_EXCEPT;
            }
            fds[i].revents = revents;
            if (revents != 0) {
                ++ready;
            }
        }
    }

    if (sets != stack_sets) {
        int saved = errno;
        free(sets);
        errno = saved;
    }
    return ready;
}
