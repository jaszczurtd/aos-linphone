/*
 * The display filter and the surface it hands pictures to.
 *
 * Each view of a surface holds two pictures: the newest one, waiting for the
 * interface, and the one the interface reads. Each filter converts into a
 * third picture of its own, then swaps it with the waiting one under the
 * surface mutex; the interface swaps the waiting picture with its own the
 * same way. A picture belongs to one place at a time, so neither side ever
 * waits for the other to convert or draw, and pixels the interface reads do
 * not change under it.
 */
#include "video_display.h"

#include <mediastreamer2/msfilter.h>
#include <mediastreamer2/msinterfaces.h>
#include <mediastreamer2/msqueue.h>
#include <mediastreamer2/msvideo.h>

#include <libyuv/convert_argb.h>
#include <libyuv/scale.h>

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIDE 4096 /* largest width or height of a view, in pixels */

/* MS_FILTER_VIDEO_NONE and MS_FILTER_VIDEO_AUTO: window ids that name no
   surface. AUTO is the default window id of the linphone core. */
#define WINDOW_NONE ((void *)0)
#define WINDOW_AUTO ((void *)-1)

struct picture {
    unsigned char *pixels;
    size_t capacity;
    int width;
    int height;
    int stride;
    int valid; /* holds a picture; 0: the view has none */
};

struct view {
    struct picture *ready; /* the newest picture, not taken yet */
    struct picture *front; /* the picture the interface reads */
    int fresh;             /* ready is newer than front */
    int width;             /* area of the view; 0: hidden */
    int height;
};

struct aos_video_surface {
    pthread_mutex_t lock; /* guards views */
    struct view views[AOS_VIDEO_VIEWS];
    aos_video_notify_fn notify;
    void *context;
};

/* ---- pictures ---- */

static void picture_free(struct picture *p) {
    if (p != NULL) {
        free(p->pixels);
        free(p);
    }
}

/* Makes room for width x height ARGB pixels. Returns 0 when out of memory;
   the picture then keeps its old contents. */
static int picture_reserve(struct picture *p, int width, int height) {
    size_t stride = (size_t)width * 4;
    size_t need = stride * (size_t)height;
    if (need > p->capacity) {
        unsigned char *pixels = realloc(p->pixels, need);
        if (pixels == NULL) {
            return 0;
        }
        p->pixels = pixels;
        p->capacity = need;
    }
    p->width = width;
    p->height = height;
    p->stride = (int)stride;
    return 1;
}

/* ---- the surface ---- */

struct aos_video_surface *aos_video_surface_create(aos_video_notify_fn notify, void *context) {
    struct aos_video_surface *s = calloc(1, sizeof(*s));
    if (s == NULL) {
        return NULL;
    }
    int locked = pthread_mutex_init(&s->lock, NULL) == 0;
    int ok = locked;
    for (int v = 0; ok && v < AOS_VIDEO_VIEWS; ++v) {
        s->views[v].ready = calloc(1, sizeof(struct picture));
        s->views[v].front = calloc(1, sizeof(struct picture));
        ok = s->views[v].ready != NULL && s->views[v].front != NULL;
    }
    if (!ok) {
        for (int v = 0; v < AOS_VIDEO_VIEWS; ++v) {
            picture_free(s->views[v].ready);
            picture_free(s->views[v].front);
        }
        if (locked) {
            pthread_mutex_destroy(&s->lock);
        }
        free(s);
        return NULL;
    }
    s->notify = notify;
    s->context = context;
    return s;
}

void aos_video_surface_destroy(struct aos_video_surface *surface) {
    if (surface == NULL) {
        return;
    }
    for (int v = 0; v < AOS_VIDEO_VIEWS; ++v) {
        picture_free(surface->views[v].ready);
        picture_free(surface->views[v].front);
    }
    pthread_mutex_destroy(&surface->lock);
    free(surface);
}

static int clamp_side(int side) {
    if (side < 0) {
        return 0;
    }
    return side > MAX_SIDE ? MAX_SIDE : side;
}

void aos_video_surface_set_size(struct aos_video_surface *surface, enum aos_video_view view, int width, int height) {
    if (surface == NULL || (unsigned)view >= AOS_VIDEO_VIEWS) {
        return;
    }
    width = clamp_side(width);
    height = clamp_side(height);
    if (width == 0 || height == 0) {
        width = height = 0;
    }
    pthread_mutex_lock(&surface->lock);
    surface->views[view].width = width;
    surface->views[view].height = height;
    pthread_mutex_unlock(&surface->lock);
}

int aos_video_surface_get_frame(struct aos_video_surface *surface, enum aos_video_view view,
                                struct aos_video_frame *frame) {
    if (surface == NULL || (unsigned)view >= AOS_VIDEO_VIEWS) {
        return 0;
    }
    struct view *v = &surface->views[view];
    pthread_mutex_lock(&surface->lock);
    if (v->fresh) {
        struct picture *taken = v->ready;
        v->ready = v->front;
        v->front = taken;
        v->fresh = 0;
    }
    /* front belongs to the interface from here on. */
    struct picture *p = v->front;
    pthread_mutex_unlock(&surface->lock);
    if (!p->valid) {
        return 0;
    }
    frame->width = p->width;
    frame->height = p->height;
    frame->stride = p->stride;
    frame->pixels = p->pixels;
    return 1;
}

/* The area of a view; 0 x 0 when it is hidden. */
static void surface_area(struct aos_video_surface *s, int view, int *width, int *height) {
    pthread_mutex_lock(&s->lock);
    *width = s->views[view].width;
    *height = s->views[view].height;
    pthread_mutex_unlock(&s->lock);
}

/* Makes the picture the newest of the view. Returns the picture it
   replaces, which now belongs to the caller. */
static struct picture *surface_publish(struct aos_video_surface *s, int view, struct picture *p) {
    pthread_mutex_lock(&s->lock);
    struct picture *old = s->views[view].ready;
    s->views[view].ready = p;
    s->views[view].fresh = 1;
    pthread_mutex_unlock(&s->lock);
    return old;
}

/* Takes the picture away from a view. Returns 1 when the interface may be
   showing one, so that it has to be told. */
static int surface_clear(struct aos_video_surface *s, int view) {
    struct view *v = &s->views[view];
    pthread_mutex_lock(&s->lock);
    int shown = v->ready->valid || v->front->valid;
    if (shown) {
        v->ready->valid = 0;
        v->fresh = 1;
    }
    pthread_mutex_unlock(&s->lock);
    return shown;
}

static void surface_notify(struct aos_video_surface *s) {
    if (s->notify != NULL) {
        s->notify(s->context);
    }
}

/* ---- the display filter ---- */

struct display {
    struct aos_video_surface *surface; /* NULL: pictures are dropped */
    int local_view_mode;               /* -1: the preview is not shown */
    struct picture *work[AOS_VIDEO_VIEWS];
    uint8_t *scaled; /* I420 picture at the size of the view */
    size_t scaled_capacity;
};

static void display_init(MSFilter *f) {
    struct display *d = calloc(1, sizeof(*d));
    if (d != NULL) {
        for (int v = 0; v < AOS_VIDEO_VIEWS; ++v) {
            d->work[v] = calloc(1, sizeof(struct picture));
        }
    }
    f->data = d;
}

static void display_uninit(MSFilter *f) {
    struct display *d = f->data;
    if (d != NULL) {
        for (int v = 0; v < AOS_VIDEO_VIEWS; ++v) {
            picture_free(d->work[v]);
        }
        free(d->scaled);
        free(d);
    }
}

/* The size of a width x height picture scaled to fit an area, proportions
   kept. */
static void fit(int width, int height, int area_width, int area_height, int *out_width, int *out_height) {
    if ((int64_t)width * area_height > (int64_t)area_width * height) {
        *out_width = area_width;
        *out_height = (int)((int64_t)height * area_width / width);
    } else {
        *out_height = area_height;
        *out_width = (int)((int64_t)width * area_height / height);
    }
    if (*out_width < 1) {
        *out_width = 1;
    }
    if (*out_height < 1) {
        *out_height = 1;
    }
}

/* Scales and converts a YUV420P picture for a view and hands it to the
   surface. Returns 1 when the view got a new picture. Called with the
   filter locked. */
static int show_picture(struct display *d, int view, mblk_t *m) {
    MSPicture in;
    int area_width, area_height;
    struct picture *out = d->work[view];
    if (out == NULL || ms_yuv_buf_init_from_mblk(&in, m) != 0) {
        return 0;
    }
    /* mediastreamer2 gives the chroma planes of an odd width a stride of
       w / 2, while libyuv reads (w + 1) / 2 samples a row: the last column
       and row of an odd size are left out. */
    int in_width = in.w & ~1, in_height = in.h & ~1;
    if (in_width <= 0 || in_height <= 0) {
        return 0;
    }
    surface_area(d->surface, view, &area_width, &area_height);
    if (area_width == 0) {
        return 0;
    }
    int width, height;
    fit(in_width, in_height, area_width, area_height, &width, &height);

    const uint8_t *planes[3] = {in.planes[0], in.planes[1], in.planes[2]};
    int strides[3] = {in.strides[0], in.strides[1], in.strides[2]};
    if (width != in_width || height != in_height) {
        int chroma_width = (width + 1) / 2, chroma_height = (height + 1) / 2;
        size_t luma = (size_t)width * (size_t)height, chroma = (size_t)chroma_width * (size_t)chroma_height;
        if (luma + 2 * chroma > d->scaled_capacity) {
            uint8_t *scaled = realloc(d->scaled, luma + 2 * chroma);
            if (scaled == NULL) {
                return 0;
            }
            d->scaled = scaled;
            d->scaled_capacity = luma + 2 * chroma;
        }
        uint8_t *y = d->scaled, *u = y + luma, *v = u + chroma;
        if (I420Scale(in.planes[0], in.strides[0], in.planes[1], in.strides[1], in.planes[2], in.strides[2], in_width,
                      in_height, y, width, u, chroma_width, v, chroma_width, width, height, kFilterBilinear) != 0) {
            return 0;
        }
        planes[0] = y;
        planes[1] = u;
        planes[2] = v;
        strides[0] = width;
        strides[1] = strides[2] = chroma_width;
    }
    if (!picture_reserve(out, width, height)) {
        return 0;
    }
    /* libyuv names formats by the order in a little-endian word: its BGRA
       has the bytes A, R, G, B in memory on every machine. */
    if (I420ToBGRA(planes[0], strides[0], planes[1], strides[1], planes[2], strides[2], out->pixels, out->stride,
                   width, height) != 0) {
        return 0;
    }
    out->valid = 1;
    d->work[view] = surface_publish(d->surface, view, out);
    return 1;
}

static void clear_views(struct display *d) {
    if (d->surface != NULL) {
        int shown = 0;
        for (int v = 0; v < AOS_VIDEO_VIEWS; ++v) {
            shown |= surface_clear(d->surface, v);
        }
        if (shown) {
            surface_notify(d->surface);
        }
    }
}

static void display_process(MSFilter *f) {
    struct display *d = f->data;
    ms_filter_lock(f);
    int changed = 0;
    for (int v = 0; v < AOS_VIDEO_VIEWS; ++v) {
        MSQueue *queue = f->inputs[v];
        if (queue == NULL) {
            continue;
        }
        /* Only the newest picture is worth showing. */
        mblk_t *m = ms_queue_peek_last(queue);
        if (m != NULL && d != NULL && d->surface != NULL && (v != AOS_VIDEO_PREVIEW || d->local_view_mode >= 0)) {
            changed |= show_picture(d, v, m);
        }
        ms_queue_flush(queue);
    }
    if (changed) {
        surface_notify(d->surface);
    }
    ms_filter_unlock(f);
}

/* The stream stops: the views keep no picture of it. */
static void display_postprocess(MSFilter *f) {
    struct display *d = f->data;
    ms_filter_lock(f);
    if (d != NULL) {
        clear_views(d);
    }
    ms_filter_unlock(f);
}

static int display_set_window(MSFilter *f, void *arg) {
    struct display *d = f->data;
    void *id = *(void **)arg;
    struct aos_video_surface *surface = id == WINDOW_NONE || id == WINDOW_AUTO ? NULL : id;
    if (d == NULL) {
        return -1;
    }
    ms_filter_lock(f);
    if (surface != d->surface) {
        clear_views(d);
        d->surface = surface;
    }
    ms_filter_unlock(f);
    return 0;
}

static int display_get_window(MSFilter *f, void *arg) {
    struct display *d = f->data;
    ms_filter_lock(f);
    *(void **)arg = d != NULL ? d->surface : NULL;
    ms_filter_unlock(f);
    return 0;
}

static int display_set_local_view_mode(MSFilter *f, void *arg) {
    struct display *d = f->data;
    if (d == NULL) {
        return -1;
    }
    ms_filter_lock(f);
    d->local_view_mode = *(int *)arg;
    if (d->local_view_mode < 0 && d->surface != NULL && surface_clear(d->surface, AOS_VIDEO_PREVIEW)) {
        surface_notify(d->surface);
    }
    ms_filter_unlock(f);
    return 0;
}

static MSFilterMethod display_methods[] = {
    {MS_VIDEO_DISPLAY_SET_NATIVE_WINDOW_ID, display_set_window},
    {MS_VIDEO_DISPLAY_GET_NATIVE_WINDOW_ID, display_get_window},
    {MS_VIDEO_DISPLAY_SET_LOCAL_VIEW_MODE, display_set_local_view_mode},
    {0, NULL},
};

static MSFilterDesc display_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = AOS_VIDEO_DISPLAY_NAME,
    .text = "Video display handing ARGB pictures to the user interface",
    .category = MS_FILTER_OTHER,
    .ninputs = 2,
    .noutputs = 0,
    .init = display_init,
    .process = display_process,
    .postprocess = display_postprocess,
    .uninit = display_uninit,
    .methods = display_methods,
    .flags = 0,
};

void aos_video_display_register(MSFactory *factory) {
    ms_factory_register_filter(factory, &display_desc);
}
