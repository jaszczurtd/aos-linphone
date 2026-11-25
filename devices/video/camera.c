/*
 * The camera filter and its camera description. Each registered source has
 * a slot whose MSWebCamDesc comes first, so the description of a camera
 * leads back to its source.
 */
#include "video/camera.h"

#include <mediastreamer2/msfilter.h>
#include <mediastreamer2/msqueue.h>
#include <mediastreamer2/msticker.h>
#include <mediastreamer2/msvideo.h>
#include <mediastreamer2/mswebcam.h>

#include <libyuv/convert.h>

#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GRABBER_STACK (128 * 1024) /* bytes of stack of the picture thread */
#define DEAD_AFTER_MS 1000         /* a source silent this long counts as 0 fps */

struct camera {
    const struct aos_video_source *source;
    pthread_mutex_t lock; /* guards frame, stop, fps, started, start_time, sent and last_frame */
    struct aos_video_format wanted;
    struct aos_video_format got; /* valid while handle is set */
    void *handle;
    MSVideoSize size;    /* of the pictures sent: got, cut to even numbers */
    mblk_t *frame;       /* newest picture not sent yet */
    float fps;           /* most pictures per second the stream takes */
    int started;         /* start_time is set */
    uint64_t start_time; /* ticker time of the first picture sent */
    uint64_t sent;       /* pictures sent since start_time */
    uint64_t last_frame; /* monotonic ms of the newest picture of the source */
    int streaming;       /* between preprocess and postprocess */
    int stop;
    int running; /* the picture thread was started */
    pthread_t thread;
};

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

/* ---- the source ---- */

static int camera_open(struct camera *c) {
    if (c->handle != NULL) {
        return 0;
    }
    c->handle = c->source->open(c->source, &c->wanted, &c->got);
    if (c->handle == NULL) {
        ms_error("AosCamera: %s: %s cannot be opened", c->source->driver, c->source->name);
        return -1;
    }
    c->size.width = c->got.width & ~1;
    c->size.height = c->got.height & ~1;
    ms_message("AosCamera: %s: %s gives %dx%d at %.1f pictures per second", c->source->driver, c->source->name,
               c->got.width, c->got.height, (double)c->got.fps);
    return 0;
}

static void camera_close(struct camera *c) {
    if (c->handle != NULL) {
        c->source->close(c->handle);
        c->handle = NULL;
    }
}

/* ---- the picture thread ---- */

static void *grabber_main(void *arg) {
    struct camera *c = arg;
    unsigned char *rgb = malloc((size_t)c->got.width * (size_t)c->got.height * 3);
    if (rgb == NULL) {
        ms_error("AosCamera: no memory for a picture");
    }
    for (;;) {
        pthread_mutex_lock(&c->lock);
        int stop = c->stop;
        pthread_mutex_unlock(&c->lock);
        if (stop || rgb == NULL) {
            break;
        }
        int got = c->source->next_frame(c->handle, rgb);
        if (got > 0) {
            continue; /* no picture this time: check stop and try again */
        }
        if (got < 0) {
            ms_error("AosCamera: %s: %s gives no more pictures", c->source->driver, c->source->name);
            break;
        }
        MSPicture pic;
        mblk_t *m = ms_yuv_buf_alloc(&pic, c->size.width, c->size.height);
        /* libyuv names formats by their order in a little-endian word: its
           RAW is red, green, blue in memory. */
        RAWToI420(rgb, c->got.width * 3, pic.planes[0], pic.strides[0], pic.planes[1], pic.strides[1], pic.planes[2],
                  pic.strides[2], c->size.width, c->size.height);
        pthread_mutex_lock(&c->lock);
        if (c->frame != NULL) {
            freemsg(c->frame);
        }
        c->frame = m;
        c->last_frame = now_ms();
        pthread_mutex_unlock(&c->lock);
    }
    free(rgb);
    return NULL;
}

/* ---- the filter ---- */

static void camera_init(MSFilter *f) {
    struct camera *c = ms_new0(struct camera, 1);
    pthread_mutex_init(&c->lock, NULL);
    c->wanted.width = MS_VIDEO_SIZE_CIF_W;
    c->wanted.height = MS_VIDEO_SIZE_CIF_H;
    c->wanted.fps = 15.0f;
    c->fps = 15.0f;
    f->data = c;
}

static void camera_uninit(MSFilter *f) {
    struct camera *c = f->data;
    camera_close(c);
    pthread_mutex_destroy(&c->lock);
    ms_free(c);
}

static void camera_preprocess(MSFilter *f) {
    struct camera *c = f->data;
    c->stop = 0;
    c->started = 0;
    c->streaming = 1;
    c->last_frame = now_ms(); /* the source gets DEAD_AFTER_MS for its first picture */
    if (camera_open(c) != 0) {
        return;
    }
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, GRABBER_STACK);
    /* Priority 0, as the interface: a new thread would otherwise take the
       priority of the thread that starts the stream. */
    struct sched_param param;
    memset(&param, 0, sizeof(param));
    pthread_attr_setschedparam(&attr, &param);
    c->running = pthread_create(&c->thread, &attr, grabber_main, c) == 0;
    pthread_attr_destroy(&attr);
    if (!c->running) {
        ms_error("AosCamera: cannot start the picture thread");
    }
}

/* Sends the newest picture once the picture rate of the stream allows
   another one; until then a newer picture may replace it. */
static void camera_process(MSFilter *f) {
    struct camera *c = f->data;
    uint64_t now = f->ticker->time;
    mblk_t *m = NULL;
    pthread_mutex_lock(&c->lock);
    if (c->frame != NULL) {
        if (!c->started) {
            c->started = 1;
            c->start_time = now;
            c->sent = 0;
        }
        if ((double)(now - c->start_time) * (double)c->fps / 1000.0 >= (double)c->sent) {
            m = c->frame;
            c->frame = NULL;
            ++c->sent;
        }
    }
    pthread_mutex_unlock(&c->lock);
    if (m != NULL) {
        mblk_set_timestamp_info(m, (uint32_t)(now * 90));
        ms_queue_put(f->outputs[0], m);
    }
}

static void camera_postprocess(MSFilter *f) {
    struct camera *c = f->data;
    if (c->running) {
        pthread_mutex_lock(&c->lock);
        c->stop = 1;
        pthread_mutex_unlock(&c->lock);
        pthread_join(c->thread, NULL);
        c->running = 0;
    }
    c->streaming = 0;
    camera_close(c);
    if (c->frame != NULL) {
        freemsg(c->frame);
        c->frame = NULL;
    }
}

/* The source may deliver another size: it chooses again when it opens. */
static int camera_set_vsize(MSFilter *f, void *arg) {
    struct camera *c = f->data;
    const MSVideoSize *size = arg;
    if (c->running) {
        return -1;
    }
    c->wanted.width = size->width;
    c->wanted.height = size->height;
    int same = (size->width == c->got.width && size->height == c->got.height) ||
               (size->width == c->size.width && size->height == c->size.height);
    if (c->handle != NULL && !same) {
        camera_close(c);
    }
    return 0;
}

static int camera_get_vsize(MSFilter *f, void *arg) {
    struct camera *c = f->data;
    MSVideoSize *size = arg;
    if (camera_open(c) == 0) {
        *size = c->size;
    } else {
        size->width = c->wanted.width;
        size->height = c->wanted.height;
    }
    return 0;
}

static int camera_get_pix_fmt(MSFilter *f, void *arg) {
    (void)f;
    *(MSPixFmt *)arg = MS_YUV420P;
    return 0;
}

static int camera_set_fps(MSFilter *f, void *arg) {
    struct camera *c = f->data;
    float fps = *(float *)arg;
    if (!(fps > 0.0f)) {
        return -1;
    }
    pthread_mutex_lock(&c->lock);
    c->wanted.fps = fps;
    c->fps = fps;
    c->started = 0; /* count again from the next picture */
    pthread_mutex_unlock(&c->lock);
    return 0;
}

/* The pictures per second the filter sends: the stream's limit, or fewer
   when the source gives fewer. While the stream runs it is 0 once the
   source has given no picture for DEAD_AFTER_MS, or could not be started:
   mediastreamer2 then replaces a camera silent for 5 s with StaticImage. */
static int camera_get_fps(MSFilter *f, void *arg) {
    struct camera *c = f->data;
    pthread_mutex_lock(&c->lock);
    float fps = c->fps;
    int dead = c->streaming && (!c->running || now_ms() - c->last_frame > DEAD_AFTER_MS);
    pthread_mutex_unlock(&c->lock);
    if (c->handle != NULL && c->got.fps < fps) {
        fps = c->got.fps;
    }
    *(float *)arg = dead ? 0.0f : fps;
    return 0;
}

static MSFilterMethod camera_methods[] = {
    {MS_FILTER_SET_VIDEO_SIZE, camera_set_vsize},
    {MS_FILTER_GET_VIDEO_SIZE, camera_get_vsize},
    {MS_FILTER_GET_PIX_FMT, camera_get_pix_fmt},
    {MS_FILTER_SET_FPS, camera_set_fps},
    {MS_FILTER_GET_FPS, camera_get_fps},
    {0, NULL},
};

static MSFilterDesc camera_filter_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = "AosCamera",
    .text = "A camera that takes its pictures from an aos-linphone video source",
    .category = MS_FILTER_OTHER,
    .ninputs = 0,
    .noutputs = 1,
    .init = camera_init,
    .preprocess = camera_preprocess,
    .process = camera_process,
    .postprocess = camera_postprocess,
    .uninit = camera_uninit,
    .methods = camera_methods,
};

/* ---- the cameras ---- */

struct slot {
    MSWebCamDesc desc; /* first, so that a description leads to its slot */
    const struct aos_video_source *source;
};

static struct slot slots[AOS_CAMERA_MAX];

static MSFilter *camera_create_reader(MSWebCam *cam) {
    MSFilter *f = ms_factory_create_filter_from_desc(ms_web_cam_get_factory(cam), &camera_filter_desc);
    if (f != NULL) {
        ((struct camera *)f->data)->source = cam->data;
    }
    return f;
}

static int listed(MSWebCamManager *manager, const MSWebCamDesc *desc) {
    for (const MSList *it = ms_web_cam_manager_get_list(manager); it != NULL; it = it->next) {
        if (((MSWebCam *)it->data)->desc == desc) {
            return 1;
        }
    }
    return 0;
}

/* Runs for each registered description, so it lists the cameras of all
   registered sources the manager does not list yet. */
static void camera_detect(MSWebCamManager *manager) {
    for (const MSList *it = manager->descs; it != NULL; it = it->next) {
        MSWebCamDesc *desc = it->data;
        if (desc->detect == camera_detect && !listed(manager, desc)) {
            const struct aos_video_source *source = ((struct slot *)desc)->source;
            MSWebCam *cam = ms_web_cam_new(desc);
            cam->name = ms_strdup(source->name);
            cam->data = (void *)source;
            ms_web_cam_manager_add_cam(manager, cam);
        }
    }
}

int aos_camera_register(MSFactory *factory, const struct aos_video_source *source) {
    struct slot *slot = NULL;
    for (int i = 0; i < AOS_CAMERA_MAX && slot == NULL; ++i) {
        if (slots[i].source == source) {
            slot = &slots[i];
        }
    }
    for (int i = 0; i < AOS_CAMERA_MAX && slot == NULL; ++i) {
        if (slots[i].source == NULL) {
            slot = &slots[i];
            slot->source = source;
            slot->desc.driver_type = source->driver;
            slot->desc.detect = camera_detect;
            slot->desc.create_reader = camera_create_reader;
        }
    }
    if (slot == NULL) {
        return -1;
    }
    ms_web_cam_manager_register_desc(ms_factory_get_web_cam_manager(factory), &slot->desc);
    return 0;
}
