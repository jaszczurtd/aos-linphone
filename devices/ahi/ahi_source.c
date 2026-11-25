/*
 * Recording from ahi.device: a capture thread reads CMD_READ portions into
 * a ring of sample frames, and read() takes them out under a mutex.
 */
#include "ahi/ahi_source.h"

#include <mediastreamer2/mscommon.h>

#include <devices/ahi.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RATE           48000
#define PORTION_FRAMES (RATE * 20 / 1000) /* 20 ms per CMD_READ */
#define RING_FRAMES    (RATE / 2)         /* 500 ms; older frames are dropped */
#define READ_TIMEOUT_S 2                  /* read() gives up without sound this long */
#define CAPTURE_STACK  (64 * 1024)

struct capture {
    unsigned long unit;
    pthread_mutex_t lock; /* guards everything below */
    pthread_cond_t cond;  /* signalled on new frames or failure */
    short ring[RING_FRAMES];
    size_t head;  /* oldest frame */
    size_t count; /* frames in the ring */
    unsigned long dropped;
    int stop;
    int failed;
    int running;
    pthread_t thread;
};

/* Both under the lock. */
static void ring_put(struct capture *c, const short *frames, size_t n) {
    if (n > RING_FRAMES) {
        frames += n - RING_FRAMES;
        n = RING_FRAMES;
    }
    if (c->count + n > RING_FRAMES) {
        size_t drop = c->count + n - RING_FRAMES;
        c->head = (c->head + drop) % RING_FRAMES;
        c->count -= drop;
        c->dropped += drop;
    }
    size_t tail = (c->head + c->count) % RING_FRAMES;
    size_t first = n < RING_FRAMES - tail ? n : RING_FRAMES - tail;
    memcpy(c->ring + tail, frames, first * sizeof(short));
    memcpy(c->ring, frames + first, (n - first) * sizeof(short));
    c->count += n;
}

static void ring_get(struct capture *c, short *frames, size_t n) {
    size_t first = n < RING_FRAMES - c->head ? n : RING_FRAMES - c->head;
    memcpy(frames, c->ring + c->head, first * sizeof(short));
    memcpy(frames + first, c->ring, (n - first) * sizeof(short));
    c->head = (c->head + n) % RING_FRAMES;
    c->count -= n;
}

static void fail(struct capture *c) {
    pthread_mutex_lock(&c->lock);
    c->failed = 1;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->lock);
}

/* Owns the port and the request: Exec signals belong to the task that
   allocated them. */
static void *capture_main(void *arg) {
    struct capture *c = arg;
    struct MsgPort *port = CreateMsgPort();
    struct AHIRequest *req = port != NULL ? (struct AHIRequest *)CreateIORequest(port, sizeof(*req)) : NULL;
    short *buffer = AllocVec(PORTION_FRAMES * sizeof(short), MEMF_PUBLIC);
    int opened = 0;
    if (req != NULL) {
        req->ahir_Version = 4;
        opened = OpenDevice((CONST_STRPTR)AHINAME, c->unit, (struct IORequest *)req, 0) == 0;
    }
    if (!opened || buffer == NULL) {
        ms_error("AosAhiSource: cannot open %s unit %lu", AHINAME, c->unit);
        fail(c);
    } else {
        ULONG offset = 0; /* continues where the request before ended */
        for (;;) {
            pthread_mutex_lock(&c->lock);
            int stop = c->stop;
            pthread_mutex_unlock(&c->lock);
            if (stop) {
                break;
            }
            /* CMD_READ trashes the other fields: set them all each time. */
            req->ahir_Std.io_Command = CMD_READ;
            req->ahir_Std.io_Data = buffer;
            req->ahir_Std.io_Length = PORTION_FRAMES * sizeof(short);
            req->ahir_Std.io_Offset = offset;
            req->ahir_Type = AHIST_M16S;
            req->ahir_Frequency = RATE;
            if (DoIO((struct IORequest *)req) != 0) {
                ms_error("AosAhiSource: CMD_READ on unit %lu failed with %d", c->unit, (int)req->ahir_Std.io_Error);
                fail(c);
                break;
            }
            offset = req->ahir_Std.io_Offset;
            pthread_mutex_lock(&c->lock);
            ring_put(c, buffer, req->ahir_Std.io_Actual / sizeof(short));
            pthread_cond_broadcast(&c->cond);
            pthread_mutex_unlock(&c->lock);
        }
    }
    FreeVec(buffer);
    if (opened) {
        CloseDevice((struct IORequest *)req);
    }
    if (req != NULL) {
        DeleteIORequest((struct IORequest *)req);
    }
    if (port != NULL) {
        DeleteMsgPort(port);
    }
    return NULL;
}

static void *ahi_open(const struct aos_audio_source *source, const struct aos_audio_format *wanted,
                      struct aos_audio_format *got) {
    (void)wanted; /* ahi.device converts, mediastreamer2 resamples better */
    const struct aos_ahi_source *s = source->data;
    struct capture *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        return NULL;
    }
    c->unit = s->unit;
    pthread_mutex_init(&c->lock, NULL);
    pthread_cond_init(&c->cond, NULL);
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, CAPTURE_STACK);
    /* The priority of the MSTicker threads, as the AHI writer. */
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    pthread_attr_setschedparam(&attr, &param);
    c->running = pthread_create(&c->thread, &attr, capture_main, c) == 0;
    pthread_attr_destroy(&attr);
    if (!c->running) {
        pthread_cond_destroy(&c->cond);
        pthread_mutex_destroy(&c->lock);
        free(c);
        return NULL;
    }
    got->rate = RATE;
    got->nchannels = 1;
    return c;
}

static int ahi_read(void *handle, short *samples, int frames) {
    struct capture *c = handle;
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += READ_TIMEOUT_S;
    int result = -1;
    pthread_mutex_lock(&c->lock);
    while (!c->failed && c->count < (size_t)frames) {
        if (pthread_cond_timedwait(&c->cond, &c->lock, &deadline) != 0) {
            break;
        }
    }
    if (c->count >= (size_t)frames) {
        ring_get(c, samples, (size_t)frames);
        result = 0;
    }
    pthread_mutex_unlock(&c->lock);
    return result;
}

static void ahi_close(void *handle) {
    struct capture *c = handle;
    pthread_mutex_lock(&c->lock);
    c->stop = 1;
    pthread_mutex_unlock(&c->lock);
    pthread_join(c->thread, NULL);
    if (c->dropped > 0) {
        ms_message("AosAhiSource: %lu sample frames dropped", c->dropped);
    }
    pthread_cond_destroy(&c->cond);
    pthread_mutex_destroy(&c->lock);
    free(c);
}

void aos_ahi_source_init(struct aos_ahi_source *s, unsigned long unit) {
    memset(s, 0, sizeof(*s));
    s->unit = unit;
    snprintf(s->name, sizeof(s->name), "AHI unit %lu", unit);
    s->source.name = s->name;
    s->source.open = ahi_open;
    s->source.read = ahi_read;
    s->source.close = ahi_close;
    s->source.data = s;
}
