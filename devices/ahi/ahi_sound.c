/*
 * AHI playback and capture for mediastreamer2. The playback filter runs in
 * the MSTicker thread and only queues the samples; its writer thread feeds
 * ahi.device, which paces the output at the sample rate of the stream. The
 * capture filter works the other way round: its reader thread takes the
 * samples from the capture source, which paces them, and queues them for
 * the filter.
 */
#include "ahi_sound.h"

#include <mediastreamer2/msfilter.h>
#include <mediastreamer2/msqueue.h>
#include <mediastreamer2/mssndcard.h>
#include <mediastreamer2/msticker.h>

#include <devices/ahi.h>
#include <dos/dos.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/exec.h>

#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define BLOCK_MS      40          /* length of one CMD_WRITE */
#define READ_MS       20          /* length of one portion of captured sound */
#define MAX_QUEUED_MS 300         /* older samples are dropped beyond this */
#define WRITER_STACK  (64 * 1024) /* bytes of stack of a writer or reader thread */

struct ahi_out {
    pthread_mutex_t lock; /* guards queue, rate, nchannels, stop, dropped and played */
    MSBufferizer *queue;
    int rate;
    int nchannels;
    int stop;
    size_t dropped;  /* bytes the filter dropped because the writer fell behind */
    uint64_t played; /* sample frames ahi.device has played */
    /* Drives the ticker by the clock of the card: see ahi_write_preprocess(). */
    MSTickerSynchronizer *sync;
    MSTicker *synced; /* the ticker the synchronizer is set on */
    int running;
    struct Task *task; /* the writer thread, which ahi_write_postprocess() wakes */
    pthread_t thread;
    ULONG unit;
};

/* Bytes of MAX_QUEUED_MS of sound, in whole sample frames: dropping a
   part of a frame would shift every later sample. */
static size_t queue_limit(int rate, int nchannels) {
    return (size_t)rate * MAX_QUEUED_MS / 1000 * 2 * (size_t)nchannels;
}

/* ---- the writer thread ---- */

static void setup_request(struct AHIRequest *req, UBYTE *data, ULONG bytes, int rate, int nchannels,
                          struct AHIRequest *link) {
    req->ahir_Std.io_Command = CMD_WRITE;
    req->ahir_Std.io_Data = data;
    req->ahir_Std.io_Length = bytes;
    req->ahir_Std.io_Offset = 0;
    req->ahir_Type = nchannels == 2 ? AHIST_S16S : AHIST_M16S;
    req->ahir_Frequency = rate;
    req->ahir_Volume = 0x10000;
    req->ahir_Position = 0x8000;
    req->ahir_Link = link;
}

/* Waits until req completes, or until stop is set and the thread gets
   SIGBREAKF_CTRL_F: a device that stalls must not keep the filter from
   stopping. Returns 0 when req completed (and was taken back), -1 on stop
   with req still pending. */
static int wait_request(struct ahi_out *out, struct AHIRequest *req, ULONG port_mask) {
    for (;;) {
        if (CheckIO((struct IORequest *)req)) {
            WaitIO((struct IORequest *)req);
            return 0;
        }
        pthread_mutex_lock(&out->lock);
        int stop = out->stop;
        pthread_mutex_unlock(&out->lock);
        if (stop) {
            return -1;
        }
        Wait(port_mask | SIGBREAKF_CTRL_F);
    }
}

/* Plays the queue in blocks of BLOCK_MS on two requests, each linked to the
   one before, so the device plays them back to back; a block the queue
   cannot fill is completed with silence. */
static void *writer_main(void *arg) {
    struct ahi_out *out = arg;
    struct MsgPort *port = CreateMsgPort();
    struct AHIRequest *req[2] = {NULL, NULL};
    UBYTE *buffer[2] = {NULL, NULL};
    int opened = 0;

    pthread_mutex_lock(&out->lock);
    int rate = out->rate, nchannels = out->nchannels;
    pthread_mutex_unlock(&out->lock);
    ULONG block = (ULONG)(rate * BLOCK_MS / 1000) * 2 * (ULONG)nchannels;

    if (port != NULL) {
        req[0] = (struct AHIRequest *)CreateIORequest(port, sizeof(struct AHIRequest));
    }
    if (req[0] != NULL) {
        req[0]->ahir_Version = 4;
        opened = OpenDevice((CONST_STRPTR)AHINAME, out->unit, (struct IORequest *)req[0], 0) == 0;
    }
    if (opened) {
        req[1] = AllocVec(sizeof(struct AHIRequest), MEMF_PUBLIC);
        buffer[0] = AllocVec(block, MEMF_PUBLIC);
        buffer[1] = AllocVec(block, MEMF_PUBLIC);
    }
    if (!opened || req[1] == NULL || buffer[0] == NULL || buffer[1] == NULL) {
        ms_error("AosAhiWrite: cannot open %s unit %lu", AHINAME, (unsigned long)out->unit);
    } else {
        CopyMem(req[0], req[1], sizeof(struct AHIRequest));
        ms_message("AosAhiWrite: playing at %d Hz, %d channel(s)", rate, nchannels);
        pthread_mutex_lock(&out->lock);
        out->task = FindTask(NULL);
        pthread_mutex_unlock(&out->lock);
        ULONG port_mask = 1UL << port->mp_SigBit;
        struct AHIRequest *previous = NULL;
        /* The writer takes whole blocks only and keeps one more block of
           margin in the queue, for a ticker that comes late. Without enough
           sound it plays silence until the queue holds a block and the
           margin again, instead of cutting the sound into pieces. Reported
           when the stream ends: blocks of silence at the start (filling)
           and after the queue ran dry (gaps). */
        unsigned long blocks = 0, filling = 0, gaps = 0;
        /* Blocks that ended more than a quarter block late after the one
           before: the writer came back too late, and the card ran out of
           sound for a moment. */
        unsigned long late = 0;
        long longest = 0;
        struct timespec last_done = {0, 0};
        int refill = 1;
        for (int i = 0;; i ^= 1) {
            pthread_mutex_lock(&out->lock);
            int stop = out->stop;
            size_t got = 0;
            if (!stop) {
                size_t avail = ms_bufferizer_get_avail(out->queue);
                if (refill && avail >= 2 * block) {
                    refill = 0;
                }
                if (!refill && avail >= block) {
                    got = ms_bufferizer_read(out->queue, buffer[i], block);
                } else if (!refill) {
                    refill = 1; /* ran dry: a gap now, then fill up again */
                    ++gaps;
                }
            }
            pthread_mutex_unlock(&out->lock);
            if (stop) {
                break;
            }
            ++blocks;
            if (got < block) {
                memset(buffer[i] + got, 0, block - got);
                if (gaps == 0) {
                    ++filling;
                }
            }
            setup_request(req[i], buffer[i], block, rate, nchannels, previous);
            SendIO((struct IORequest *)req[i]);
            if (previous != NULL) {
                if (wait_request(out, previous, port_mask) != 0) {
                    /* Stopped: the post-loop code takes back previous. */
                    AbortIO((struct IORequest *)req[i]);
                    WaitIO((struct IORequest *)req[i]);
                    break;
                }
                struct timespec done;
                clock_gettime(CLOCK_MONOTONIC, &done);
                if (last_done.tv_sec != 0) {
                    long ms = (long)(done.tv_sec - last_done.tv_sec) * 1000 +
                              (done.tv_nsec - last_done.tv_nsec) / 1000000;
                    if (ms > BLOCK_MS + BLOCK_MS / 4) {
                        ++late;
                    }
                    if (ms > longest) {
                        longest = ms;
                    }
                }
                last_done = done;
                pthread_mutex_lock(&out->lock);
                out->played += (uint64_t)(block / (2 * (ULONG)nchannels));
                pthread_mutex_unlock(&out->lock);
            }
            previous = req[i];
        }
        if (previous != NULL) {
            AbortIO((struct IORequest *)previous);
            WaitIO((struct IORequest *)previous);
        }
        pthread_mutex_lock(&out->lock);
        size_t dropped = out->dropped;
        pthread_mutex_unlock(&out->lock);
        ms_message("AosAhiWrite: %lu blocks of %d ms, %lu silent at the start, %lu gaps, %lu ms dropped, %lu late "
                   "(longest %ld ms)",
                   blocks, BLOCK_MS, filling, gaps, (unsigned long)(dropped * 1000 / ((size_t)rate * 2 * (size_t)nchannels)),
                   late, longest);
    }

    FreeVec(buffer[0]);
    FreeVec(buffer[1]);
    FreeVec(req[1]);
    if (opened) {
        CloseDevice((struct IORequest *)req[0]);
    }
    if (req[0] != NULL) {
        DeleteIORequest((struct IORequest *)req[0]);
    }
    if (port != NULL) {
        DeleteMsgPort(port);
    }
    pthread_mutex_lock(&out->lock);
    out->task = NULL; /* nothing may signal this task once it ends */
    pthread_mutex_unlock(&out->lock);
    return NULL;
}

/* ---- the playback filter ---- */

static void ahi_write_init(MSFilter *f) {
    struct ahi_out *out = ms_new0(struct ahi_out, 1);
    pthread_mutex_init(&out->lock, NULL);
    out->queue = ms_bufferizer_new();
    out->rate = 8000;
    out->nchannels = 1;
    out->unit = AHI_DEFAULT_UNIT;
    out->sync = ms_ticker_synchronizer_new();
    f->data = out;
}

static void ahi_write_uninit(MSFilter *f) {
    struct ahi_out *out = f->data;
    ms_ticker_synchronizer_destroy(out->sync);
    ms_bufferizer_destroy(out->queue);
    pthread_mutex_destroy(&out->lock);
    ms_free(out);
}

/* Starts a writer or reader thread at the priority mediastreamer2 gives its
   MSTicker threads, the top of the SCHED_RR range: otherwise the thread gets
   the priority of the thread that starts the stream, and the ticker can
   starve it. Returns nonzero when the thread runs. */
static int start_sound_thread(pthread_t *thread, void *(*main)(void *), void *arg) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, WRITER_STACK);
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    pthread_attr_setschedparam(&attr, &param);
    int running = pthread_create(thread, &attr, main, arg) == 0;
    pthread_attr_destroy(&attr);
    return running;
}

/* The ticker produces the sound on the system clock and ahi.device plays it
   on the clock of the card. When the card runs faster, the queue empties and
   blocks go out filled in part, which sounds broken; under load in a virtual
   machine the AROS clock loses up to 2%. The synchronizer corrects the time
   of the ticker by the frames the card has played, so the ticker keeps the
   pace of the card. A ticker takes one synchronizer: the card's playback is
   the reference, the capture filter sets none. */
static void ahi_write_preprocess(MSFilter *f) {
    struct ahi_out *out = f->data;
    out->stop = 0;
    out->dropped = 0;
    out->played = 0;
    out->running = start_sound_thread(&out->thread, writer_main, out);
    if (!out->running) {
        ms_error("AosAhiWrite: cannot start the writer thread");
    }
}

static void ahi_write_process(MSFilter *f) {
    struct ahi_out *out = f->data;
    mblk_t *m;
    pthread_mutex_lock(&out->lock);
    uint64_t played = out->played;
    int rate = out->rate;
    while ((m = ms_queue_get(f->inputs[0])) != NULL) {
        if (out->running) {
            ms_bufferizer_put(out->queue, m);
        } else {
            freemsg(m);
        }
    }
    size_t limit = queue_limit(out->rate, out->nchannels);
    size_t avail = ms_bufferizer_get_avail(out->queue);
    if (avail > limit) {
        ms_bufferizer_skip_bytes(out->queue, (int)(avail - limit));
        out->dropped += avail - limit;
    }
    pthread_mutex_unlock(&out->lock);
    if (out->running && out->synced == NULL) {
        /* Set in the ticker thread, which reads the time function. */
        ms_ticker_set_synchronizer(f->ticker, out->sync);
        out->synced = f->ticker;
    }
    if (out->synced != NULL) {
        ms_ticker_synchronizer_update(out->sync, played, (unsigned int)rate);
    }
}

static void ahi_write_postprocess(MSFilter *f) {
    struct ahi_out *out = f->data;
    if (out->synced != NULL) {
        ms_ticker_set_synchronizer(out->synced, NULL);
        out->synced = NULL;
    }
    if (out->running) {
        pthread_mutex_lock(&out->lock);
        out->stop = 1;
        if (out->task != NULL) {
            Signal(out->task, SIGBREAKF_CTRL_F);
        }
        pthread_mutex_unlock(&out->lock);
        pthread_join(out->thread, NULL);
        out->running = 0;
    }
    ms_bufferizer_flush(out->queue);
}

static int ahi_set_rate(MSFilter *f, void *arg) {
    struct ahi_out *out = f->data;
    pthread_mutex_lock(&out->lock);
    out->rate = *(int *)arg;
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int ahi_get_rate(MSFilter *f, void *arg) {
    struct ahi_out *out = f->data;
    *(int *)arg = out->rate;
    return 0;
}

static int ahi_set_nchannels(MSFilter *f, void *arg) {
    struct ahi_out *out = f->data;
    int n = *(int *)arg;
    if (n != 1 && n != 2) {
        return -1;
    }
    pthread_mutex_lock(&out->lock);
    out->nchannels = n;
    pthread_mutex_unlock(&out->lock);
    return 0;
}

static int ahi_get_nchannels(MSFilter *f, void *arg) {
    struct ahi_out *out = f->data;
    *(int *)arg = out->nchannels;
    return 0;
}

static MSFilterMethod ahi_write_methods[] = {
    {MS_FILTER_SET_SAMPLE_RATE, ahi_set_rate},
    {MS_FILTER_GET_SAMPLE_RATE, ahi_get_rate},
    {MS_FILTER_SET_NCHANNELS, ahi_set_nchannels},
    {MS_FILTER_GET_NCHANNELS, ahi_get_nchannels},
    {0, NULL},
};

static MSFilterDesc ahi_write_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = "AosAhiWrite",
    .text = "Sound playback through AROS ahi.device",
    .category = MS_FILTER_OTHER,
    .ninputs = 1,
    .noutputs = 0,
    .init = ahi_write_init,
    .preprocess = ahi_write_preprocess,
    .process = ahi_write_process,
    .postprocess = ahi_write_postprocess,
    .uninit = ahi_write_uninit,
    .methods = ahi_write_methods,
};

/* ---- the capture filter ---- */

struct ahi_in {
    const struct aos_audio_source *source;
    pthread_mutex_t lock; /* guards queue and stop */
    MSBufferizer *queue;
    struct aos_audio_format wanted;
    struct aos_audio_format got; /* valid while handle is set */
    void *handle;
    int stop;
    int running;
    pthread_t thread;
};

static int reader_open(struct ahi_in *in) {
    if (in->handle != NULL) {
        return 0;
    }
    in->handle = in->source->open(in->source, &in->wanted, &in->got);
    if (in->handle == NULL) {
        ms_error("AosAhiRead: the capture source %s cannot be opened", in->source->name);
        return -1;
    }
    ms_message("AosAhiRead: %s gives %d Hz, %d channel(s)", in->source->name, in->got.rate, in->got.nchannels);
    return 0;
}

static void reader_close(struct ahi_in *in) {
    if (in->handle != NULL) {
        in->source->close(in->handle);
        in->handle = NULL;
    }
}

/* Takes portions of READ_MS from the source, which returns each one when it
   has been captured, and queues them for the filter. */
static void *reader_main(void *arg) {
    struct ahi_in *in = arg;
    int frames = in->got.rate * READ_MS / 1000;
    size_t bytes = (size_t)frames * 2 * (size_t)in->got.nchannels;
    size_t limit = queue_limit(in->got.rate, in->got.nchannels);
    for (;;) {
        pthread_mutex_lock(&in->lock);
        int stop = in->stop;
        pthread_mutex_unlock(&in->lock);
        if (stop) {
            break;
        }
        mblk_t *m = allocb(bytes, 0);
        if (in->source->read(in->handle, (short *)m->b_wptr, frames) != 0) {
            ms_error("AosAhiRead: the capture source %s gives no more sound", in->source->name);
            freemsg(m);
            break;
        }
        m->b_wptr += bytes;
        pthread_mutex_lock(&in->lock);
        ms_bufferizer_put(in->queue, m);
        size_t avail = ms_bufferizer_get_avail(in->queue);
        if (avail > limit) {
            ms_bufferizer_skip_bytes(in->queue, (int)(avail - limit));
        }
        pthread_mutex_unlock(&in->lock);
        if (avail > limit) {
            /* The filter does not take the samples, or the source returns
               them faster than they are captured: wait a portion, so that
               this thread, at the priority of the ticker, cannot hold the
               processor. */
            struct timespec pause = {0, READ_MS * 1000000L};
            nanosleep(&pause, NULL);
        }
    }
    return NULL;
}

static void ahi_read_init(MSFilter *f) {
    struct ahi_in *in = ms_new0(struct ahi_in, 1);
    pthread_mutex_init(&in->lock, NULL);
    in->queue = ms_bufferizer_new();
    in->wanted.rate = 8000;
    in->wanted.nchannels = 1;
    f->data = in;
}

static void ahi_read_uninit(MSFilter *f) {
    struct ahi_in *in = f->data;
    reader_close(in);
    ms_bufferizer_destroy(in->queue);
    pthread_mutex_destroy(&in->lock);
    ms_free(in);
}

static void ahi_read_preprocess(MSFilter *f) {
    struct ahi_in *in = f->data;
    in->stop = 0;
    if (reader_open(in) != 0) {
        return;
    }
    in->running = start_sound_thread(&in->thread, reader_main, in);
    if (!in->running) {
        ms_error("AosAhiRead: cannot start the reader thread");
    }
}

/* Sends on everything captured since the last tick. */
static void ahi_read_process(MSFilter *f) {
    struct ahi_in *in = f->data;
    mblk_t *m = NULL;
    pthread_mutex_lock(&in->lock);
    size_t avail = ms_bufferizer_get_avail(in->queue);
    if (avail > 0) {
        m = allocb(avail, 0);
        ms_bufferizer_read(in->queue, m->b_wptr, avail);
        m->b_wptr += avail;
    }
    pthread_mutex_unlock(&in->lock);
    if (m != NULL) {
        ms_queue_put(f->outputs[0], m);
    }
}

static void ahi_read_postprocess(MSFilter *f) {
    struct ahi_in *in = f->data;
    if (in->running) {
        pthread_mutex_lock(&in->lock);
        in->stop = 1;
        pthread_mutex_unlock(&in->lock);
        pthread_join(in->thread, NULL);
        in->running = 0;
    }
    reader_close(in);
    ms_bufferizer_flush(in->queue);
}

/* The source may deliver another rate or number of channels: it chooses
   again when it opens, and mediastreamer2 resamples what it gets. */
static int ahi_read_set_rate(MSFilter *f, void *arg) {
    struct ahi_in *in = f->data;
    if (in->running) {
        return -1;
    }
    in->wanted.rate = *(int *)arg;
    if (in->handle != NULL && in->got.rate != in->wanted.rate) {
        reader_close(in);
    }
    return 0;
}

static int ahi_read_get_rate(MSFilter *f, void *arg) {
    struct ahi_in *in = f->data;
    *(int *)arg = reader_open(in) == 0 ? in->got.rate : in->wanted.rate;
    return 0;
}

static int ahi_read_set_nchannels(MSFilter *f, void *arg) {
    struct ahi_in *in = f->data;
    int n = *(int *)arg;
    if (in->running || (n != 1 && n != 2)) {
        return -1;
    }
    in->wanted.nchannels = n;
    if (in->handle != NULL && in->got.nchannels != n) {
        reader_close(in);
    }
    return 0;
}

static int ahi_read_get_nchannels(MSFilter *f, void *arg) {
    struct ahi_in *in = f->data;
    *(int *)arg = reader_open(in) == 0 ? in->got.nchannels : in->wanted.nchannels;
    return 0;
}

static MSFilterMethod ahi_read_methods[] = {
    {MS_FILTER_SET_SAMPLE_RATE, ahi_read_set_rate},
    {MS_FILTER_GET_SAMPLE_RATE, ahi_read_get_rate},
    {MS_FILTER_SET_NCHANNELS, ahi_read_set_nchannels},
    {MS_FILTER_GET_NCHANNELS, ahi_read_get_nchannels},
    {0, NULL},
};

static MSFilterDesc ahi_read_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = "AosAhiRead",
    .text = "Sound capture of the AROS AHI card from an aos-linphone sound source",
    .category = MS_FILTER_OTHER,
    .ninputs = 0,
    .noutputs = 1,
    .init = ahi_read_init,
    .preprocess = ahi_read_preprocess,
    .process = ahi_read_process,
    .postprocess = ahi_read_postprocess,
    .uninit = ahi_read_uninit,
    .methods = ahi_read_methods,
};

/* ---- the card ---- */

/* The capture source of the cards detected from now on, in every factory:
   a program has one. */
static const struct aos_audio_source *capture_source;

static MSFilter *ahi_create_writer(MSSndCard *card) {
    return ms_factory_create_filter_from_desc(ms_snd_card_get_factory(card), &ahi_write_desc);
}

static MSFilter *ahi_create_reader(MSSndCard *card) {
    if (card->data == NULL) {
        return NULL;
    }
    MSFilter *f = ms_factory_create_filter_from_desc(ms_snd_card_get_factory(card), &ahi_read_desc);
    if (f != NULL) {
        ((struct ahi_in *)f->data)->source = card->data;
    }
    return f;
}

static void ahi_detect(MSSndCardManager *manager);

static MSSndCardDesc ahi_card_desc = {
    .driver_type = "AHI",
    .detect = ahi_detect,
    .create_reader = ahi_create_reader,
    .create_writer = ahi_create_writer,
};

static void ahi_detect(MSSndCardManager *manager) {
    MSSndCard *card = ms_snd_card_new_with_name(&ahi_card_desc, "default unit");
    card->capabilities = MS_SND_CARD_CAP_PLAYBACK;
    if (capture_source != NULL) {
        card->capabilities |= MS_SND_CARD_CAP_CAPTURE;
        card->data = (void *)capture_source;
    }
    ms_snd_card_manager_add_card(manager, card);
}

void aos_ahi_register(MSFactory *factory, const struct aos_audio_source *capture) {
    capture_source = capture;
    ms_snd_card_manager_register_desc(ms_factory_get_snd_card_manager(factory), &ahi_card_desc);
}
