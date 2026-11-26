/*
 * select() and poll() over WaitSelect().
 *
 * The sets and entries hold libaosnet socket numbers; WaitSelect() gets the
 * calling thread's NetStack descriptors of the same sockets (net_fd.c),
 * which are below the 256 of the task's descriptor table. Only sockets can
 * be waited on: select() fails with EBADF on any other descriptor, poll()
 * reports it as POLLNVAL. With no descriptors, both sleep for the timeout
 * (ortp's timer uses select() this way).
 */
#include "net_internal.h"

#include <poll.h>
#include <stdlib.h>
#include <string.h>

/* One waited socket: its number and the thread's descriptor. */
struct aos_wait_fd {
    int fd;
    int nsfd;
};

int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout) {
    if (nfds < 0) {
        errno = EINVAL;
        return -1;
    }
    if (nfds > FD_SETSIZE) {
        nfds = FD_SETSIZE;
    }
    if (!aos_ns_available()) {
        errno = ENETDOWN;
        return -1;
    }

    struct aos_wait_fd waited[AOS_SOCK_MAX];
    int count = 0, top = -1;
    fd_set r, w, e;
    FD_ZERO(&r);
    FD_ZERO(&w);
    FD_ZERO(&e);
    for (int fd = 0; fd < nfds; ++fd) {
        int in_r = readfds != NULL && FD_ISSET(fd, readfds);
        int in_w = writefds != NULL && FD_ISSET(fd, writefds);
        int in_e = exceptfds != NULL && FD_ISSET(fd, exceptfds);
        if (!in_r && !in_w && !in_e) {
            continue;
        }
        int nsfd = aos_sock_get(fd);
        if (nsfd < 0) {
            errno = EBADF;
            return -1;
        }
        if (in_r) {
            FD_SET(nsfd, &r);
        }
        if (in_w) {
            FD_SET(nsfd, &w);
        }
        if (in_e) {
            FD_SET(nsfd, &e);
        }
        waited[count].fd = fd;
        waited[count].nsfd = nsfd;
        ++count;
        if (nsfd > top) {
            top = nsfd;
        }
    }

    int rc = aos_ns_wait(top + 1, readfds != NULL ? &r : NULL, writefds != NULL ? &w : NULL,
                         exceptfds != NULL ? &e : NULL, timeout, NULL);
    if (rc < 0) {
        return -1;
    }

    int ready = 0;
    for (int i = 0; i < count; ++i) {
        int fd = waited[i].fd, nsfd = waited[i].nsfd;
        if (readfds != NULL && FD_ISSET(fd, readfds) && !FD_ISSET(nsfd, &r)) {
            FD_CLR(fd, readfds);
        }
        if (writefds != NULL && FD_ISSET(fd, writefds) && !FD_ISSET(nsfd, &w)) {
            FD_CLR(fd, writefds);
        }
        if (exceptfds != NULL && FD_ISSET(fd, exceptfds) && !FD_ISSET(nsfd, &e)) {
            FD_CLR(fd, exceptfds);
        }
        ready += (readfds != NULL && FD_ISSET(fd, readfds)) + (writefds != NULL && FD_ISSET(fd, writefds)) +
                 (exceptfds != NULL && FD_ISSET(fd, exceptfds));
    }
    return ready;
}

#define AOS_POLL_READ   (POLLIN | POLLRDNORM)
#define AOS_POLL_WRITE  (POLLOUT | POLLWRNORM | POLLWRBAND)
#define AOS_POLL_EXCEPT (POLLPRI | POLLRDBAND)

/* Entries handled with a descriptor array on the stack. */
#define AOS_POLL_STACK 64

int poll(struct pollfd *fds, nfds_t nfds, int timeout) {
    if (nfds > 0 && fds == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (!aos_ns_available()) {
        errno = ENETDOWN;
        return -1;
    }

    int stack_nsfd[AOS_POLL_STACK];
    int *nsfds = stack_nsfd;
    if (nfds > AOS_POLL_STACK) {
        nsfds = malloc(nfds * sizeof(*nsfds));
        if (nsfds == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }

    fd_set r, w, e;
    FD_ZERO(&r);
    FD_ZERO(&w);
    FD_ZERO(&e);
    int top = -1, invalid = 0;
    for (nfds_t i = 0; i < nfds; ++i) {
        fds[i].revents = 0;
        nsfds[i] = -1;
        if (fds[i].fd < 0) {
            continue;
        }
        int nsfd = aos_sock_get(fds[i].fd);
        if (nsfd < 0) {
            fds[i].revents = POLLNVAL;
            ++invalid;
            continue;
        }
        nsfds[i] = nsfd;
        if (fds[i].events & AOS_POLL_READ) {
            FD_SET(nsfd, &r);
        }
        if (fds[i].events & AOS_POLL_WRITE) {
            FD_SET(nsfd, &w);
        }
        if (fds[i].events & AOS_POLL_EXCEPT) {
            FD_SET(nsfd, &e);
        }
        if (nsfd > top) {
            top = nsfd;
        }
    }

    struct timeval tv;
    struct timeval *tvp = NULL;
    if (invalid > 0) {
        /* POSIX: an invalid entry makes poll() return at once. */
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        tvp = &tv;
    } else if (timeout >= 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = (timeout % 1000) * 1000;
        tvp = &tv;
    }

    int ready = -1;
    if (aos_ns_wait(top + 1, &r, &w, &e, tvp, NULL) >= 0) {
        ready = invalid;
        for (nfds_t i = 0; i < nfds; ++i) {
            int nsfd = nsfds[i];
            if (nsfd < 0) {
                continue;
            }
            short revents = 0;
            if (FD_ISSET(nsfd, &r)) {
                revents |= fds[i].events & AOS_POLL_READ;
            }
            if (FD_ISSET(nsfd, &w)) {
                revents |= fds[i].events & AOS_POLL_WRITE;
            }
            if (FD_ISSET(nsfd, &e)) {
                revents |= fds[i].events & AOS_POLL_EXCEPT;
            }
            fds[i].revents = revents;
            if (revents != 0) {
                ++ready;
            }
        }
    }

    if (nsfds != stack_nsfd) {
        int saved = errno;
        free(nsfds);
        errno = saved;
    }
    return ready;
}
