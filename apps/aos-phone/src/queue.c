#include "queue.h"

#include <string.h>

int aos_queue_init(struct aos_queue *queue, void *storage, size_t record_size, unsigned capacity) {
    queue->storage = storage;
    queue->record_size = record_size;
    queue->capacity = capacity;
    queue->head = 0;
    queue->count = 0;
    return pthread_mutex_init(&queue->lock, NULL) == 0 ? 0 : -1;
}

void aos_queue_destroy(struct aos_queue *queue) {
    pthread_mutex_destroy(&queue->lock);
}

int aos_queue_push(struct aos_queue *queue, const void *record) {
    int result = -1;
    pthread_mutex_lock(&queue->lock);
    if (queue->count < queue->capacity) {
        unsigned tail = (queue->head + queue->count) % queue->capacity;
        memcpy(queue->storage + (size_t)tail * queue->record_size, record, queue->record_size);
        ++queue->count;
        result = 0;
    }
    pthread_mutex_unlock(&queue->lock);
    return result;
}

int aos_queue_pop(struct aos_queue *queue, void *record) {
    int result = 0;
    pthread_mutex_lock(&queue->lock);
    if (queue->count > 0) {
        memcpy(record, queue->storage + (size_t)queue->head * queue->record_size, queue->record_size);
        queue->head = (queue->head + 1) % queue->capacity;
        --queue->count;
        result = 1;
    }
    pthread_mutex_unlock(&queue->lock);
    return result;
}
