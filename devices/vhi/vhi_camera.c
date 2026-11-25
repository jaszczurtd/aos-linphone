/*
 * Camera sources over the VHI libraries of uvc.class. The library is opened
 * per stream; the grabs run in the thread of the camera filter.
 */
#include "vhi/vhi_camera.h"
#include "source.h"
#include "video/camera.h"

#include <mediastreamer2/mscommon.h>

#include <devices/uvc_capture.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <vhi/vhi.h>

#define __VHI_NOLIBBASE__
#include <proto/vhi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct vhi_camera {
    struct aos_video_source source;
    char library[16]; /* uvc<n>.vhi */
    char name[128];
};

struct vhi_handle {
    struct Library *base;
    APTR vh;
    ULONG width;
    ULONG height;
};

static struct vhi_camera cameras[AOS_VHI_UNITS];

static IPTR vhi(struct Library *VHIBase, ULONG method, ULONG submethod, APTR attr, APTR handle, ULONG *err) {
    ULONG e = 0;
    IPTR result = vhi_api(method, submethod, attr, &e, handle);
    if (err != NULL) {
        *err = e;
    }
    return result;
}

static ULONG distance(ULONG a, ULONG b) {
    return a > b ? a - b : b - a;
}

/* The YUY2 mode nearest to the wanted size, or -1 when there is none. */
static LONG pick_mode(struct vhi_handle *h, const struct aos_video_format *wanted, struct UVCModeInfo *chosen) {
    ULONG count = (ULONG)vhi(h->base, VHI_METHOD_GET, VHI_UVC_MODECOUNT, NULL, h->vh, NULL);
    ULONG best = ~(ULONG)0;
    LONG index = -1;
    for (ULONG i = 0; i < count; ++i) {
        struct VHIUVCModeQuery query;
        memset(&query, 0, sizeof(query));
        query.size = sizeof(query);
        query.index = i;
        if (!vhi(h->base, VHI_METHOD_GET, VHI_UVC_MODEINFO, &query, h->vh, NULL) ||
            query.mode.pixel_format != UVC_MODE_YUY2) {
            continue;
        }
        ULONG d = distance(query.mode.width, (ULONG)wanted->width) + distance(query.mode.height, (ULONG)wanted->height);
        if (d < best) {
            best = d;
            index = (LONG)i;
            *chosen = query.mode;
        }
    }
    return index;
}

static void *vhi_open(const struct aos_video_source *source, const struct aos_video_format *wanted,
                      struct aos_video_format *got) {
    const struct vhi_camera *cam = source->data;
    struct vhi_handle *h = calloc(1, sizeof(*h));
    if (h == NULL) {
        return NULL;
    }
    h->base = OpenLibrary((CONST_STRPTR)cam->library, 0);
    if (h->base != NULL) {
        h->vh = (APTR)vhi(h->base, VHI_METHOD_OPEN, 0, NULL, NULL, NULL);
    }
    struct UVCModeInfo mode;
    memset(&mode, 0, sizeof(mode));
    LONG index = h->vh != NULL ? pick_mode(h, wanted, &mode) : -1;
    if (index < 0) {
        ms_error("AosVhi: %s has no uncompressed mode", cam->library);
        if (h->vh != NULL) {
            vhi(h->base, VHI_METHOD_CLOSE, 0, NULL, h->vh, NULL);
        }
        if (h->base != NULL) {
            CloseLibrary(h->base);
        }
        free(h);
        return NULL;
    }
    vhi(h->base, VHI_METHOD_SET, VHI_UVC_MODE, (APTR)(IPTR)index, h->vh, NULL);
    ULONG interval = mode.capture_interval ? mode.capture_interval : mode.default_interval;
    h->width = mode.width;
    h->height = mode.height;
    got->width = (int)mode.width;
    got->height = (int)mode.height;
    got->fps = interval ? 1e7f / (float)interval : 15.0f;
    ms_message("AosVhi: %s mode %ld: YUY2 %lux%lu at %.1f pictures per second", cam->library, (long)index,
               (unsigned long)mode.width, (unsigned long)mode.height, (double)got->fps);
    return h;
}

static int vhi_next_frame(void *handle, unsigned char *rgb) {
    struct vhi_handle *h = handle;
    struct vhi_digitize dig;
    ULONG err = 0;
    memset(&dig, 0, sizeof(dig));
    struct vhi_image *img = (struct vhi_image *)vhi(h->base, VHI_METHOD_PERFORM, VHI_DIGITIZE_PICTURE, &dig, h->vh, &err);
    if (img == NULL) {
        return err == VHI_ERR_NO_HARDWARE ? -1 : 1;
    }
    int ok = img->type == VHI_RGB_24 && img->width == h->width && img->height == h->height && img->chunky != NULL;
    if (ok) {
        memcpy(rgb, img->chunky, (size_t)h->width * h->height * 3);
    }
    FreeVec(img->chunky);
    FreeVec(img);
    return ok ? 0 : 1;
}

static void vhi_close(void *handle) {
    struct vhi_handle *h = handle;
    vhi(h->base, VHI_METHOD_CLOSE, 0, NULL, h->vh, NULL);
    CloseLibrary(h->base);
    free(h);
}

int aos_vhi_cameras_register(MSFactory *factory) {
    int added = 0;
    for (int unit = 0; unit < AOS_VHI_UNITS; ++unit) {
        struct vhi_camera *cam = &cameras[unit];
        snprintf(cam->library, sizeof(cam->library), "uvc%d.vhi", unit);
        struct Library *base = OpenLibrary((CONST_STRPTR)cam->library, 0);
        if (base == NULL) {
            continue;
        }
        APTR vh = (APTR)vhi(base, VHI_METHOD_OPEN, 0, NULL, NULL, NULL);
        const char *card = vh != NULL ? (const char *)vhi(base, VHI_METHOD_GET, VHI_CARD_NAME, NULL, vh, NULL) : NULL;
        snprintf(cam->name, sizeof(cam->name), "%s (uvc%d)", card != NULL ? card : "camera", unit);
        if (vh != NULL) {
            vhi(base, VHI_METHOD_CLOSE, 0, NULL, vh, NULL);
        }
        CloseLibrary(base);
        cam->source.driver = "VHI";
        cam->source.name = cam->name;
        cam->source.open = vhi_open;
        cam->source.next_frame = vhi_next_frame;
        cam->source.close = vhi_close;
        cam->source.data = cam;
        if (aos_camera_register(factory, &cam->source) == 0) {
            ++added;
        }
    }
    return added;
}
