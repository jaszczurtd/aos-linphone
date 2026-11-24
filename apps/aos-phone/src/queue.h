/*
 * queue - a fixed-size FIFO of fixed-size records shared by two threads.
 *
 * The caller supplies the storage, so the queue never allocates. Push and
 * pop copy whole records under a mutex and never block: push fails when the
 * queue is full, pop reports an empty queue.
 */
#ifndef AOS_PHONE_QUEUE_H
#define AOS_PHONE_QUEUE_H

#include <pthread.h>
#include <stddef.h>

struct aos_queue {
    pthread_mutex_t lock;
    unsigned char *storage;
    size_t record_size;
    unsigned capacity;
    unsigned head;  /* index of the oldest record */
    unsigned count; /* records in the queue */
};

/* Prepares the queue on storage of capacity * record_size bytes.
   Returns 0, or -1 when the mutex cannot be created. */
int aos_queue_init(struct aos_queue *queue, void *storage, size_t record_size, unsigned capacity);

void aos_queue_destroy(struct aos_queue *queue);

/* Copies the record to the end of the queue. Returns 0, or -1 when the
   queue is full. */
int aos_queue_push(struct aos_queue *queue, const void *record);

/* Moves the oldest record to *record. Returns 1, or 0 when the queue is
   empty. */
int aos_queue_pop(struct aos_queue *queue, void *record);

#endif
