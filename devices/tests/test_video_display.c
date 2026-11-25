/*
 * Tests of the video display filter and its surface, without a call: the
 * filter comes from a mediastreamer2 factory, and the test puts YUV420P
 * pictures of one colour into its input queues and runs its process
 * function, as an MSTicker would.
 *
 * Checked: the byte order of the pictures (alpha, red, green, blue), their
 * size in the area of a view with the proportions kept, also for odd sizes,
 * a hidden view, a picture the interface holds while new ones arrive, the
 * preview input and its local view mode, the end of the stream, a change of
 * surface, and window ids that name no surface.
 */
#include "aos_test.h"
#include "video/video_display.h"

#include <mediastreamer2/msfilter.h>
#include <mediastreamer2/msinterfaces.h>
#include <mediastreamer2/msqueue.h>
#include <mediastreamer2/msvideo.h>

#include <stdlib.h>
#include <string.h>

/* BT.601 limited-range YUV of pure red, green and blue. */
static const unsigned char RED[3] = {81, 90, 240};
static const unsigned char GREEN[3] = {145, 54, 34};
static const unsigned char BLUE[3] = {41, 240, 110};

static MSFilter *display;
static MSQueue inputs[AOS_VIDEO_VIEWS];
static int notified;

static void on_notify(void *context) {
    (void)context;
    ++notified;
}

static void put_picture(int input, int width, int height, const unsigned char yuv[3]) {
    MSPicture pic;
    mblk_t *m = ms_yuv_buf_alloc(&pic, width, height);
    for (int plane = 0; plane < 3; ++plane) {
        int rows = plane == 0 ? pic.h : (pic.h + 1) / 2;
        memset(pic.planes[plane], yuv[plane], (size_t)pic.strides[plane] * (size_t)rows);
    }
    ms_queue_put(&inputs[input], m);
}

static void process(void) {
    display->desc->process(display);
}

static void set_window(void *id) {
    ms_filter_call_method(display, MS_VIDEO_DISPLAY_SET_NATIVE_WINDOW_ID, &id);
}

/* Every pixel has the expected bytes, within the rounding of the YUV
   conversion. */
static int all_pixels(const struct aos_video_frame *frame, int r, int g, int b) {
    for (int y = 0; y < frame->height; ++y) {
        const unsigned char *p = frame->pixels + (size_t)y * (size_t)frame->stride;
        for (int x = 0; x < frame->width; ++x, p += 4) {
            if (p[0] != 255 || abs(p[1] - r) > 8 || abs(p[2] - g) > 8 || abs(p[3] - b) > 8) {
                return 0;
            }
        }
    }
    return 1;
}

static int frame_size(struct aos_video_surface *s, enum aos_video_view view, int width, int height) {
    struct aos_video_frame frame;
    return aos_video_surface_get_frame(s, view, &frame) && frame.width == width && frame.height == height;
}

int main(void) {
    MSFactory *factory = ms_factory_new();
    aos_video_display_register(factory);
    display = ms_factory_create_filter_from_name(factory, AOS_VIDEO_DISPLAY_NAME);
    struct aos_video_surface *s = aos_video_surface_create(on_notify, NULL);
    AOS_CHECK(display != NULL && s != NULL, "the filter and the surface are created");
    if (display == NULL || s == NULL) {
        return aos_test_result();
    }
    for (int v = 0; v < AOS_VIDEO_VIEWS; ++v) {
        ms_queue_init(&inputs[v]);
        display->inputs[v] = &inputs[v];
    }
    struct aos_video_frame frame;

    /* No window id yet: pictures go nowhere. */
    aos_video_surface_set_size(s, AOS_VIDEO_REMOTE, 320, 240);
    put_picture(0, 320, 240, RED);
    process();
    AOS_CHECK(!aos_video_surface_get_frame(s, AOS_VIDEO_REMOTE, &frame) && notified == 0,
              "without a window id the view gets no picture");
    AOS_CHECK(ms_queue_empty(&inputs[0]), "the filter empties its input");

    set_window(s);
    put_picture(0, 320, 240, RED);
    process();
    int got = aos_video_surface_get_frame(s, AOS_VIDEO_REMOTE, &frame);
    AOS_CHECK(got && notified == 1, "a picture reaches the view and the interface is told");
    AOS_CHECK(got && frame.width == 320 && frame.height == 240 && frame.stride >= 320 * 4,
              "a picture that fits keeps its size");
    AOS_CHECK(got && all_pixels(&frame, 255, 0, 0), "red comes out as the bytes A, R, G, B = 255, 255, 0, 0");

    /* The interface keeps the red picture while a blue and a green one
       arrive. */
    struct aos_video_frame held = frame;
    put_picture(0, 320, 240, BLUE);
    process();
    put_picture(0, 320, 240, GREEN);
    process();
    AOS_CHECK(all_pixels(&held, 255, 0, 0), "a taken picture does not change");
    got = aos_video_surface_get_frame(s, AOS_VIDEO_REMOTE, &frame);
    AOS_CHECK(got && all_pixels(&frame, 0, 255, 0), "the next call takes the newest picture, green");
    AOS_CHECK(got && aos_video_surface_get_frame(s, AOS_VIDEO_REMOTE, &frame) && all_pixels(&frame, 0, 255, 0),
              "without a new picture the same one comes again");

    /* Proportions. */
    aos_video_surface_set_size(s, AOS_VIDEO_REMOTE, 200, 200);
    put_picture(0, 320, 240, RED);
    process();
    AOS_CHECK(frame_size(s, AOS_VIDEO_REMOTE, 200, 150), "320x240 in a 200x200 area becomes 200x150");
    put_picture(0, 240, 320, RED);
    process();
    AOS_CHECK(frame_size(s, AOS_VIDEO_REMOTE, 150, 200), "240x320 in a 200x200 area becomes 150x200");
    aos_video_surface_set_size(s, AOS_VIDEO_REMOTE, 641, 400);
    put_picture(0, 160, 120, RED);
    process();
    got = aos_video_surface_get_frame(s, AOS_VIDEO_REMOTE, &frame);
    AOS_CHECK(got && frame.width == 533 && frame.height == 400 && all_pixels(&frame, 255, 0, 0),
              "160x120 grows to 533x400 in a 641x400 area, still red");
    aos_video_surface_set_size(s, AOS_VIDEO_REMOTE, 80, 60);
    put_picture(0, 161, 121, BLUE);
    process();
    got = aos_video_surface_get_frame(s, AOS_VIDEO_REMOTE, &frame);
    AOS_CHECK(got && frame.width == 80 && frame.height == 60 && all_pixels(&frame, 0, 0, 255),
              "161x121 loses its odd column and row and shrinks to 80x60, still blue");
    aos_video_surface_set_size(s, AOS_VIDEO_REMOTE, 533, 400);

    /* A hidden view. */
    aos_video_surface_set_size(s, AOS_VIDEO_REMOTE, 0, 0);
    int before = notified;
    put_picture(0, 320, 240, BLUE);
    process();
    AOS_CHECK(notified == before && frame_size(s, AOS_VIDEO_REMOTE, 80, 60),
              "a hidden view drops new pictures and keeps the last one");
    aos_video_surface_set_size(s, AOS_VIDEO_REMOTE, -5, 100);
    put_picture(0, 320, 240, BLUE);
    process();
    AOS_CHECK(notified == before, "a negative size hides the view too");
    aos_video_surface_set_size(s, AOS_VIDEO_REMOTE, 320, 240);

    /* The preview on input 1, unless the local view is off. */
    AOS_CHECK(!aos_video_surface_get_frame(s, AOS_VIDEO_PREVIEW, &frame), "the preview has no picture at first");
    aos_video_surface_set_size(s, AOS_VIDEO_PREVIEW, 160, 120);
    put_picture(1, 320, 240, BLUE);
    process();
    got = aos_video_surface_get_frame(s, AOS_VIDEO_PREVIEW, &frame);
    AOS_CHECK(got && frame.width == 160 && frame.height == 120 && all_pixels(&frame, 0, 0, 255),
              "input 1 goes to the preview");
    int mode = -1;
    ms_filter_call_method(display, MS_VIDEO_DISPLAY_SET_LOCAL_VIEW_MODE, &mode);
    AOS_CHECK(!aos_video_surface_get_frame(s, AOS_VIDEO_PREVIEW, &frame), "local view mode -1 removes the preview");
    put_picture(1, 320, 240, BLUE);
    process();
    AOS_CHECK(!aos_video_surface_get_frame(s, AOS_VIDEO_PREVIEW, &frame) && ms_queue_empty(&inputs[1]),
              "with local view mode -1 the preview stays empty");
    mode = 0;
    ms_filter_call_method(display, MS_VIDEO_DISPLAY_SET_LOCAL_VIEW_MODE, &mode);

    /* The end of the stream. */
    put_picture(0, 320, 240, RED);
    put_picture(1, 320, 240, RED);
    process();
    before = notified;
    display->desc->postprocess(display);
    AOS_CHECK(notified == before + 1, "the end of the stream tells the interface");
    AOS_CHECK(!aos_video_surface_get_frame(s, AOS_VIDEO_REMOTE, &frame) &&
                  !aos_video_surface_get_frame(s, AOS_VIDEO_PREVIEW, &frame),
              "after the end of the stream the views have no picture");

    /* Another surface: the old one loses its picture and is told. */
    struct aos_video_surface *other = aos_video_surface_create(NULL, NULL);
    aos_video_surface_set_size(other, AOS_VIDEO_REMOTE, 320, 240);
    put_picture(0, 320, 240, RED);
    process();
    before = notified;
    set_window(other);
    put_picture(0, 320, 240, GREEN);
    process();
    got = aos_video_surface_get_frame(other, AOS_VIDEO_REMOTE, &frame);
    AOS_CHECK(notified == before + 1 && !aos_video_surface_get_frame(s, AOS_VIDEO_REMOTE, &frame),
              "a new window id takes the picture away from the old surface");
    AOS_CHECK(got && all_pixels(&frame, 0, 255, 0), "pictures then go to the new surface");
    set_window(s);
    aos_video_surface_destroy(other);

    /* Window ids that name no surface. */
    void *id = NULL;
    ms_filter_call_method(display, MS_VIDEO_DISPLAY_GET_NATIVE_WINDOW_ID, &id);
    AOS_CHECK(id == s, "the window id reads back as the surface");
    set_window((void *)-1);
    ms_filter_call_method(display, MS_VIDEO_DISPLAY_GET_NATIVE_WINDOW_ID, &id);
    before = notified;
    put_picture(0, 320, 240, RED);
    process();
    AOS_CHECK(id == NULL && notified == before && !aos_video_surface_get_frame(s, AOS_VIDEO_REMOTE, &frame),
              "the window id LINPHONE_VIDEO_DISPLAY_AUTO names no surface");

    for (int v = 0; v < AOS_VIDEO_VIEWS; ++v) {
        display->inputs[v] = NULL;
    }
    ms_filter_destroy(display);
    aos_video_surface_destroy(s);
    ms_factory_destroy(factory);
    return aos_test_result();
}
