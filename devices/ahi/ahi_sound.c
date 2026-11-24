/*
 * AHI playback for mediastreamer2. The filter runs in the MSTicker thread
 * and only queues the samples; its writer thread feeds ahi.device, which
 * paces the output at the sample rate of the stream.
 */
#include "ahi_sound.h"

#include <mediastreamer2/msfilter.h>
#include <mediastreamer2/msqueue.h>
#include <mediastreamer2/mssndcard.h>

#include <devices/ahi.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <proto/exec.h>

#include <pthread.h>
#include <sched.h>
#include <string.h>

#define BLOCK_MS      40          /* length of one CMD_WRITE */
#define MAX_QUEUED_MS 300         /* older samples are dropped beyond this */
#define WRITER_STACK  (64 * 1024) /* bytes of stack of a writer thread */

struct ahi_out {
    pthread_mutex_t lock; /* guards queue, rate, nchannels and stop */
    MSBufferizer *queue;
    int rate;
    int nchannels;
    int stop;
    int running;
    pthread_t thread;
    ULONG unit;
};

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
        struct AHIRequest *previous = NULL;
        for (int i = 0;; i ^= 1) {
            pthread_mutex_lock(&out->lock);
            int stop = out->stop;
            size_t got = 0;
            if (!stop) {
                size_t avail = ms_bufferizer_get_avail(out->queue);
                got = ms_bufferizer_read(out->queue, buffer[i], avail < block ? avail : block);
            }
            pthread_mutex_unlock(&out->lock);
            if (stop) {
                break;
            }
            if (got < block) {
                memset(buffer[i] + got, 0, block - got);
            }
            setup_request(req[i], buffer[i], block, rate, nchannels, previous);
            SendIO((struct IORequest *)req[i]);
            if (previous != NULL) {
                WaitIO((struct IORequest *)previous);
            }
            previous = req[i];
        }
        if (previous != NULL) {
            AbortIO((struct IORequest *)previous);
            WaitIO((struct IORequest *)previous);
        }
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
    f->data = out;
}

static void ahi_write_uninit(MSFilter *f) {
    struct ahi_out *out = f->data;
    ms_bufferizer_destroy(out->queue);
    pthread_mutex_destroy(&out->lock);
    ms_free(out);
}

static void ahi_write_preprocess(MSFilter *f) {
    struct ahi_out *out = f->data;
    out->stop = 0;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, WRITER_STACK);
    /* The priority mediastreamer2 gives its MSTicker threads, the top of the
       SCHED_RR range: otherwise the writer gets the priority of the thread
       that starts the stream, and the ticker can starve it. */
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = sched_get_priority_max(SCHED_RR);
    pthread_attr_setschedparam(&attr, &param);
    out->running = pthread_create(&out->thread, &attr, writer_main, out) == 0;
    pthread_attr_destroy(&attr);
    if (!out->running) {
        ms_error("AosAhiWrite: cannot start the writer thread");
    }
}

static void ahi_write_process(MSFilter *f) {
    struct ahi_out *out = f->data;
    mblk_t *m;
    pthread_mutex_lock(&out->lock);
    while ((m = ms_queue_get(f->inputs[0])) != NULL) {
        if (out->running) {
            ms_bufferizer_put(out->queue, m);
        } else {
            freemsg(m);
        }
    }
    size_t limit = (size_t)out->rate * 2 * (size_t)out->nchannels * MAX_QUEUED_MS / 1000;
    size_t avail = ms_bufferizer_get_avail(out->queue);
    if (avail > limit) {
        ms_bufferizer_skip_bytes(out->queue, (int)(avail - limit));
    }
    pthread_mutex_unlock(&out->lock);
}

static void ahi_write_postprocess(MSFilter *f) {
    struct ahi_out *out = f->data;
    if (out->running) {
        pthread_mutex_lock(&out->lock);
        out->stop = 1;
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

/* ---- the card ---- */

static MSFilter *ahi_create_writer(MSSndCard *card) {
    return ms_factory_create_filter_from_desc(ms_snd_card_get_factory(card), &ahi_write_desc);
}

static void ahi_detect(MSSndCardManager *manager);

static MSSndCardDesc ahi_card_desc = {
    .driver_type = "AHI",
    .detect = ahi_detect,
    .create_writer = ahi_create_writer,
};

static void ahi_detect(MSSndCardManager *manager) {
    MSSndCard *card = ms_snd_card_new_with_name(&ahi_card_desc, "default unit");
    card->capabilities = MS_SND_CARD_CAP_PLAYBACK;
    ms_snd_card_manager_add_card(manager, card);
}

void aos_ahi_register(MSFactory *factory) {
    ms_snd_card_manager_register_desc(ms_factory_get_snd_card_manager(factory), &ahi_card_desc);
}
