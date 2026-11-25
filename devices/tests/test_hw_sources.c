/*
 * Tests of the hardware sources of the device layer: the microphone source
 * on ahi.device and the cameras of uvc.class (VHI). They need the devices:
 * AHI unit 0 (any AHI driver; in a VM without a sound client it records
 * silence, which is enough here) and, for the camera part, a USB camera
 * that uvc.class has published as uvc0.vhi. Without such a camera the
 * camera part is skipped. Neither device may be in use by another program,
 * aos-phone in a call for one.
 *
 * Checked: the name and format of the AHI source, that its portions come
 * at the pace of the recording, and that it closes while recording; the
 * VHI camera id, the mode it picks for a size the camera does not have
 * (the nearest uncompressed one, compared with the mode list of the VHI
 * library), the pixel format and picture rate it reports, and pictures of
 * that size coming through the camera filter on a real MSTicker, no faster
 * than the rate asked for.
 */
#include "aos_test.h"
#include "ahi/ahi_source.h"
#include "vhi/vhi_camera.h"

#include <mediastreamer2/msfilter.h>
#include <mediastreamer2/msqueue.h>
#include <mediastreamer2/msticker.h>
#include <mediastreamer2/msvideo.h>
#include <mediastreamer2/mswebcam.h>

#include <devices/uvc_capture.h>
#include <exec/types.h>
#include <proto/exec.h>
#include <vhi/vhi.h>

#define __VHI_NOLIBBASE__
#include <proto/vhi.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORTION  960 /* 20 ms at 48 kHz */
#define PORTIONS 50  /* 1 s */

static void test_ahi_source(void) {
    static struct aos_ahi_source mic;
    aos_ahi_source_init(&mic, 0);
    AOS_CHECK(strcmp(mic.source.name, "AHI unit 0") == 0, "AHI: the source is named after its unit");
    struct aos_audio_format wanted = {16000, 1}, got = {0, 0};
    void *h = mic.source.open(&mic.source, &wanted, &got);
    AOS_CHECK(h != NULL && got.rate == 48000 && got.nchannels == 1, "AHI: the source records 48 kHz mono");
    if (h == NULL) {
        return;
    }
    static short samples[PORTION];
    /* The first portion waits for the device to open and start. */
    int ok = mic.source.read(h, samples, PORTION) == 0;
    long start = aos_test_now_ms();
    for (int i = 0; i < PORTIONS && ok; ++i) {
        ok = mic.source.read(h, samples, PORTION) == 0;
    }
    long elapsed = aos_test_now_ms() - start;
    printf("info: %d portions of 20 ms took %ld ms\n", PORTIONS, elapsed);
    AOS_CHECK(ok, "AHI: every portion arrives");
    /* The ring buffers up to 500 ms, so the first reads may return at once;
       the AROS clock in a VM also runs a little slow under load. */
    AOS_CHECK(elapsed >= 500 && elapsed <= 1500, "AHI: portions come at the pace of the recording");
    long closing = aos_test_now_ms();
    mic.source.close(h);
    AOS_CHECK(aos_test_now_ms() - closing < 1000, "AHI: the source closes while recording");
}

/* ---- a sink that counts the pictures of a camera ---- */

static struct {
    pthread_mutex_t lock;
    int width, height; /* expected size */
    int pictures;
    int bad_pictures; /* of another size */
} sink;

static void video_sink_process(MSFilter *f) {
    mblk_t *m;
    while ((m = ms_queue_get(f->inputs[0])) != NULL) {
        MSPicture pic;
        ms_yuv_buf_init_from_mblk(&pic, m);
        pthread_mutex_lock(&sink.lock);
        ++sink.pictures;
        sink.bad_pictures += pic.w != sink.width || pic.h != sink.height;
        pthread_mutex_unlock(&sink.lock);
        freemsg(m);
    }
}

static MSFilterDesc video_sink_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = "TestVideoSink",
    .category = MS_FILTER_OTHER,
    .ninputs = 1,
    .process = video_sink_process,
};

/* The YUY2 mode of uvc<unit>.vhi nearest to w x h (sum of the differences
   of width and height), read from the library itself. */
static int nearest_mode(int unit, int w, int h, MSVideoSize *out) {
    char name[16];
    snprintf(name, sizeof(name), "uvc%d.vhi", unit);
    struct Library *VHIBase = OpenLibrary((CONST_STRPTR)name, 0);
    if (VHIBase == NULL) {
        return -1;
    }
    ULONG err = 0;
    APTR vh = (APTR)vhi_api(VHI_METHOD_OPEN, 0, NULL, &err, NULL);
    int best = -1;
    if (vh != NULL) {
        ULONG count = (ULONG)vhi_api(VHI_METHOD_GET, VHI_UVC_MODECOUNT, NULL, &err, vh);
        for (ULONG i = 0; i < count; ++i) {
            struct VHIUVCModeQuery q;
            memset(&q, 0, sizeof(q));
            q.size = sizeof(q);
            q.index = i;
            if (!vhi_api(VHI_METHOD_GET, VHI_UVC_MODEINFO, &q, &err, vh) || q.mode.pixel_format != UVC_MODE_YUY2) {
                continue;
            }
            int d = abs((int)q.mode.width - w) + abs((int)q.mode.height - h);
            if (best < 0 || d < best) {
                best = d;
                out->width = (int)q.mode.width;
                out->height = (int)q.mode.height;
            }
        }
        vhi_api(VHI_METHOD_CLOSE, 0, NULL, &err, vh);
    }
    CloseLibrary(VHIBase);
    return best;
}

static MSWebCam *find_vhi_camera(MSFactory *factory) {
    const bctbx_list_t *it = ms_web_cam_manager_get_list(ms_factory_get_web_cam_manager(factory));
    for (; it != NULL; it = it->next) {
        MSWebCam *cam = it->data;
        if (strncmp(ms_web_cam_get_string_id(cam), "VHI: ", 5) == 0) {
            return cam;
        }
    }
    return NULL;
}

static void test_vhi_camera(void) {
    MSFactory *factory = ms_factory_new_with_voip();
    int count = aos_vhi_cameras_register(factory);
    printf("info: %d VHI camera(s)\n", count);
    if (count == 0) {
        printf("SKIP: no camera of uvc.class (uvc0.vhi to uvc3.vhi)\n");
        ms_factory_destroy(factory);
        return;
    }
    MSWebCam *cam = find_vhi_camera(factory);
    const char *id = cam != NULL ? ms_web_cam_get_string_id(cam) : "";
    printf("info: camera %s\n", id);
    AOS_CHECK(cam != NULL && strstr(id, "(uvc") != NULL,
              "VHI: the camera is registered as \"VHI: <card> (uvc<n>)\"");
    MSFilter *reader = cam != NULL ? ms_web_cam_create_reader(cam) : NULL;
    AOS_CHECK(reader != NULL, "VHI: the camera filter is created");
    if (reader == NULL) {
        ms_factory_destroy(factory);
        return;
    }
    /* CIF is not a mode of usual USB cameras; the source picks the nearest
       uncompressed mode, which the filter reports once it has opened it. */
    MSVideoSize size = {MS_VIDEO_SIZE_CIF_W, MS_VIDEO_SIZE_CIF_H};
    float fps = 15.0f;
    ms_filter_call_method(reader, MS_FILTER_SET_VIDEO_SIZE, &size);
    ms_filter_call_method(reader, MS_FILTER_SET_FPS, &fps);
    MSPixFmt format = MS_PIX_FMT_UNKNOWN;
    ms_filter_call_method(reader, MS_FILTER_GET_PIX_FMT, &format);
    memset(&size, 0, sizeof(size));
    ms_filter_call_method(reader, MS_FILTER_GET_VIDEO_SIZE, &size);
    printf("info: CIF asked, %dx%d picked\n", size.width, size.height);
    AOS_CHECK(format == MS_YUV420P, "VHI: the filter sends YUV420P");
    const char *unit = strstr(id, "(uvc");
    MSVideoSize nearest = {0, 0};
    int found = unit != NULL ? nearest_mode(unit[4] - '0', MS_VIDEO_SIZE_CIF_W, MS_VIDEO_SIZE_CIF_H, &nearest) : -1;
    printf("info: nearest YUY2 mode of the camera: %dx%d\n", nearest.width, nearest.height);
    AOS_CHECK(found >= 0 && size.width == (nearest.width & ~1) && size.height == (nearest.height & ~1),
              "VHI: a size the camera lacks gives the nearest uncompressed mode it has");

    pthread_mutex_lock(&sink.lock);
    sink.width = size.width;
    sink.height = size.height;
    sink.pictures = sink.bad_pictures = 0;
    pthread_mutex_unlock(&sink.lock);
    MSFilter *out = ms_factory_create_filter_from_desc(factory, &video_sink_desc);
    MSTicker *ticker = ms_ticker_new();
    ms_filter_link(reader, 0, out, 0);
    ms_ticker_attach(ticker, reader);
    aos_test_sleep_ms(3000);
    float rate = 0.0f;
    ms_filter_call_method(reader, MS_FILTER_GET_FPS, &rate);
    ms_ticker_detach(ticker, reader);
    ms_filter_unlink(reader, 0, out, 0);
    ms_ticker_destroy(ticker);
    ms_filter_destroy(out);
    ms_filter_destroy(reader);

    pthread_mutex_lock(&sink.lock);
    printf("info: %d pictures in 3 s, %d of another size; %.1f per second reported\n", sink.pictures,
           sink.bad_pictures, (double)rate);
    /* USB cameras slow down in poor light; about 3 per second is the low bound. */
    AOS_CHECK(sink.pictures >= 10 && sink.bad_pictures == 0, "VHI: pictures of the picked size come through");
    AOS_CHECK(sink.pictures <= 3 * 15 + 2, "VHI: the filter keeps to the picture rate asked for");
    AOS_CHECK(rate > 0.0f && rate <= 15.5f, "VHI: the filter reports a live rate within the one asked for");
    pthread_mutex_unlock(&sink.lock);
    ms_factory_destroy(factory);
}

int main(void) {
    pthread_mutex_init(&sink.lock, NULL);
    test_ahi_source();
    test_vhi_camera();
    pthread_mutex_destroy(&sink.lock);
    return aos_test_result();
}
