/*
 * Tests of the queue between the interface and the core thread: empty and
 * full boundaries, order across the end of the storage, and one producer
 * thread against one consumer thread.
 */
#include "aos_test.h"
#include "queue.h"

#include <pthread.h>
#include <sched.h>
#include <string.h>

#define CAPACITY 5
#define COUNT    20000
#define TIMEOUT  30000 /* ms for the two-thread run */

/* An odd size, so that a wrong record offset shows. */
struct record {
    unsigned seq;
    unsigned char fill[13];
};

static struct record storage[CAPACITY];
static struct aos_queue queue;

static struct record make(unsigned seq) {
    struct record r;
    r.seq = seq;
    memset(r.fill, (int)(seq & 0xff), sizeof(r.fill));
    return r;
}

static int intact(const struct record *r, unsigned seq) {
    for (size_t i = 0; i < sizeof(r->fill); ++i) {
        if (r->fill[i] != (unsigned char)(seq & 0xff)) {
            return 0;
        }
    }
    return r->seq == seq;
}

static void boundaries(void) {
    struct record r;
    AOS_CHECK(aos_queue_pop(&queue, &r) == 0, "pop from an empty queue reports it empty");

    int pushed = 1;
    for (unsigned i = 0; i < CAPACITY; ++i) {
        struct record in = make(i);
        pushed = pushed && aos_queue_push(&queue, &in) == 0;
    }
    AOS_CHECK(pushed, "the queue takes as many records as its capacity");
    struct record extra = make(99);
    AOS_CHECK(aos_queue_push(&queue, &extra) == -1, "push to a full queue fails");

    /* Take two, add two: the new records wrap to the start of the storage. */
    int ok = aos_queue_pop(&queue, &r) == 1 && intact(&r, 0) && aos_queue_pop(&queue, &r) == 1 && intact(&r, 1);
    struct record a = make(CAPACITY), b = make(CAPACITY + 1);
    ok = ok && aos_queue_push(&queue, &a) == 0 && aos_queue_push(&queue, &b) == 0;
    for (unsigned want = 2; want < CAPACITY + 2; ++want) {
        ok = ok && aos_queue_pop(&queue, &r) == 1 && intact(&r, want);
    }
    AOS_CHECK(ok, "records come out whole and in order across the end of the storage");
    AOS_CHECK(aos_queue_pop(&queue, &r) == 0, "the queue is empty again after taking everything");
}

static void *producer(void *arg) {
    (void)arg;
    long start = aos_test_now_ms();
    for (unsigned seq = 0; seq < COUNT && aos_test_now_ms() - start < TIMEOUT;) {
        struct record r = make(seq);
        if (aos_queue_push(&queue, &r) == 0) {
            ++seq;
        } else {
            sched_yield();
        }
    }
    return NULL;
}

static void two_threads(void) {
    pthread_t thread;
    if (pthread_create(&thread, NULL, producer, NULL) != 0) {
        AOS_CHECK(0, "producer thread started");
        return;
    }
    unsigned expected = 0, damaged = 0;
    long start = aos_test_now_ms();
    while (expected < COUNT && aos_test_now_ms() - start < TIMEOUT) {
        struct record r;
        if (aos_queue_pop(&queue, &r)) {
            if (!intact(&r, expected)) {
                ++damaged;
            }
            ++expected;
        } else {
            sched_yield();
        }
    }
    pthread_join(thread, NULL);
    AOS_CHECK(expected == COUNT, "the consumer received every record of the producer");
    AOS_CHECK(damaged == 0, "every record arrived whole and in order");
}

int main(void) {
    if (aos_queue_init(&queue, storage, sizeof(struct record), CAPACITY) != 0) {
        AOS_CHECK(0, "queue created");
        return aos_test_result();
    }
    boundaries();
    two_threads();
    aos_queue_destroy(&queue);
    return aos_test_result();
}
