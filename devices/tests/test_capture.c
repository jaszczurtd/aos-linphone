/*
 * Tests of the capture side of the device layer: the file sources, the
 * camera and the capture part of the AHI card. The devices come from a
 * mediastreamer2 factory and run on a real MSTicker, linked to a small
 * filter that checks what they send.
 *
 * Checked: the pictures and samples of a file in order and again from the
 * start after its end, without the broken rest after the last whole
 * picture or sample; the pace of both sources, and that they skip lost
 * time instead of catching up; the messages of a bad setup; the camera id,
 * also after a reload, its size, pixel format and picture (colours, row
 * order and orientation after the conversion from RGB24), the picture
 * rate the stream asks for, and 0 pictures per second once its source
 * stops; the capture capability of the card, the rate, channels and
 * samples of its capture filter, and whole sample frames when it drops
 * the oldest sound from a source that runs ahead.
 */
#include "aos_test.h"
#include "ahi/ahi_sound.h"
#include "files/file_source.h"
#include "video/camera.h"

#include <mediastreamer2/msfilter.h>
#include <mediastreamer2/msqueue.h>
#include <mediastreamer2/mssndcard.h>
#include <mediastreamer2/msticker.h>
#include <mediastreamer2/msvideo.h>
#include <mediastreamer2/mswebcam.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIDEO_FILE "RAM:aos-test-capture.rgb"
#define SOUND_FILE "RAM:aos-test-capture.raw"
#define VIDEO_W    9 /* odd on purpose: the camera sends 8x6 */
#define VIDEO_H    7
#define SPLIT      4 /* first column and row of the right and bottom quarters */
#define VIDEO_FPS  20.0f
#define SOUND_RATE 8000
#define SOUND_LEN  1000 /* samples in the file: 0, 1, ... 999 */

static const unsigned char COLOURS[3][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
/* BT.601 limited-range YUV of pure red, green and blue. */
static const unsigned char YUV[3][3] = {{81, 90, 240}, {145, 54, 34}, {41, 240, 110}};

/* Picture k of the file has four quarters in different colours, so that a
   flipped, mirrored or skewed picture differs from it. */
static int quarter_colour(int picture, int x, int y) {
    static const int offset[2][2] = {{0, 1}, {2, 0}}; /* [bottom][right] */
    return (picture + offset[y >= SPLIT][x >= SPLIT]) % 3;
}

/* ---- the test files ---- */

static int write_files(void) {
    FILE *f = fopen(VIDEO_FILE, "wb");
    if (f == NULL) {
        return -1;
    }
    for (int picture = 0; picture < 3; ++picture) {
        for (int y = 0; y < VIDEO_H; ++y) {
            for (int x = 0; x < VIDEO_W; ++x) {
                fwrite(COLOURS[quarter_colour(picture, x, y)], 1, 3, f);
            }
        }
    }
    fwrite("broken", 1, 6, f); /* less than a picture */
    fclose(f);

    f = fopen(SOUND_FILE, "wb");
    if (f == NULL) {
        return -1;
    }
    for (short i = 0; i < SOUND_LEN; ++i) {
        fwrite(&i, sizeof(i), 1, f);
    }
    fwrite("x", 1, 1, f); /* less than a sample */
    fclose(f);
    return 0;
}

static int is_picture(const unsigned char *rgb, int picture) {
    for (int y = 0; y < VIDEO_H; ++y) {
        for (int x = 0; x < VIDEO_W; ++x) {
            if (memcmp(rgb + 3 * (y * VIDEO_W + x), COLOURS[quarter_colour(picture, x, y)], 3) != 0) {
                return 0;
            }
        }
    }
    return 1;
}

/* ---- a sink that checks what a device sends ---- */

static struct {
    pthread_mutex_t lock;
    int pictures;      /* pictures received */
    int bad_pictures;  /* of the wrong size or not a picture of the file */
    int pictures_seen; /* bit per picture of the file */
    int first_picture; /* index of the first picture; -1: none or wrong */
    long frames;       /* sample frames received */
    int bad_frames;    /* sample frames that do not follow the one before */
    int next_sample;   /* value the next mono sample should have; -1: any */
    int stereo;        /* frames are (n, n + 1000) pairs instead of a ramp */
} sink;

/* The picture of the file a YUV420P picture shows, or -1. */
static int picture_of(const MSPicture *pic) {
    for (int k = 0; k < 3; ++k) {
        int match = 1;
        for (int plane = 0; plane < 3 && match; ++plane) {
            int scale = plane == 0 ? 1 : 2;
            for (int y = 0; y < pic->h / scale && match; ++y) {
                for (int x = 0; x < pic->w / scale && match; ++x) {
                    int c = quarter_colour(k, x * scale, y * scale);
                    match = abs(pic->planes[plane][y * pic->strides[plane] + x] - YUV[c][plane]) <= 3;
                }
            }
        }
        if (match) {
            return k;
        }
    }
    return -1;
}

static void video_sink_process(MSFilter *f) {
    mblk_t *m;
    while ((m = ms_queue_get(f->inputs[0])) != NULL) {
        MSPicture pic;
        ms_yuv_buf_init_from_mblk(&pic, m);
        int k = pic.w == VIDEO_W - 1 && pic.h == VIDEO_H - 1 ? picture_of(&pic) : -1;
        pthread_mutex_lock(&sink.lock);
        if (++sink.pictures == 1) {
            sink.first_picture = k;
        }
        if (k < 0) {
            ++sink.bad_pictures;
        } else {
            sink.pictures_seen |= 1 << k;
        }
        pthread_mutex_unlock(&sink.lock);
        freemsg(m);
    }
}

static void sound_sink_process(MSFilter *f) {
    mblk_t *m;
    while ((m = ms_queue_get(f->inputs[0])) != NULL) {
        pthread_mutex_lock(&sink.lock);
        size_t frame_bytes = sink.stereo ? 4 : 2;
        if ((size_t)(m->b_wptr - m->b_rptr) % frame_bytes != 0) {
            ++sink.bad_frames;
        }
        for (const unsigned char *p = m->b_rptr; p + frame_bytes <= m->b_wptr; p += frame_bytes) {
            short v[2];
            memcpy(v, p, frame_bytes);
            if (sink.stereo) {
                sink.bad_frames += v[1] != v[0] + 1000;
            } else {
                if (sink.next_sample >= 0 && v[0] != sink.next_sample) {
                    ++sink.bad_frames;
                }
                sink.next_sample = (v[0] + 1) % SOUND_LEN;
            }
            ++sink.frames;
        }
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

static MSFilterDesc sound_sink_desc = {
    .id = MS_FILTER_PLUGIN_ID,
    .name = "TestSoundSink",
    .category = MS_FILTER_OTHER,
    .ninputs = 1,
    .process = sound_sink_process,
};

/* ---- source -> sink on a ticker ---- */

struct run {
    MSFilter *source;
    MSFilter *out;
    MSTicker *ticker;
};

static void start_run(struct run *r, MSFactory *factory, MSFilter *source, MSFilterDesc *sink_desc, int stereo) {
    pthread_mutex_lock(&sink.lock);
    sink.pictures = sink.bad_pictures = sink.pictures_seen = 0;
    sink.first_picture = -1;
    sink.frames = 0;
    sink.bad_frames = 0;
    sink.next_sample = 0; /* the first sample of the file */
    sink.stereo = stereo;
    pthread_mutex_unlock(&sink.lock);
    r->source = source;
    r->out = ms_factory_create_filter_from_desc(factory, sink_desc);
    r->ticker = ms_ticker_new();
    ms_filter_link(source, 0, r->out, 0);
    ms_ticker_attach(r->ticker, source);
}

static void stop_run(struct run *r) {
    ms_ticker_detach(r->ticker, r->source);
    ms_filter_unlink(r->source, 0, r->out, 0);
    ms_ticker_destroy(r->ticker);
    ms_filter_destroy(r->out);
}

static void run(MSFactory *factory, MSFilter *source, MSFilterDesc *sink_desc, long ms) {
    struct run r;
    start_run(&r, factory, source, sink_desc, 0);
    aos_test_sleep_ms(ms);
    stop_run(&r);
}

/* ---- sources that misbehave ---- */

/* Gives 3 pictures, 50 ms apart, then no more. */
static void *failing_open(const struct aos_video_source *source, const struct aos_video_format *wanted,
                          struct aos_video_format *got) {
    (void)source;
    (void)wanted;
    static int count;
    count = 0;
    got->width = VIDEO_W;
    got->height = VIDEO_H;
    got->fps = VIDEO_FPS;
    return &count;
}

static int failing_next_frame(void *handle, unsigned char *rgb) {
    int *count = handle;
    aos_test_sleep_ms(50);
    if (++*count > 3) {
        return -1;
    }
    memset(rgb, 0, VIDEO_W * VIDEO_H * 3);
    return 0;
}

static void failing_close(void *handle) {
    (void)handle;
}

static const struct aos_video_source failing_source = {
    .driver = "Fail",
    .name = "after 3 pictures",
    .open = failing_open,
    .next_frame = failing_next_frame,
    .close = failing_close,
};

/* Gives 3 pictures, 1.3 s apart, then no more: slower than one a second. */
static void *slow_open(const struct aos_video_source *source, const struct aos_video_format *wanted,
                       struct aos_video_format *got) {
    (void)source;
    (void)wanted;
    static int count;
    count = 0;
    got->width = VIDEO_W;
    got->height = VIDEO_H;
    got->fps = VIDEO_FPS;
    return &count;
}

static int slow_next_frame(void *handle, unsigned char *rgb) {
    int *count = handle;
    aos_test_sleep_ms(1300);
    if (++*count > 3) {
        return -1;
    }
    memset(rgb, 0, VIDEO_W * VIDEO_H * 3);
    return 0;
}

static const struct aos_video_source slow_source = {
    .driver = "Slow",
    .name = "3 pictures 1.3 s apart",
    .open = slow_open,
    .next_frame = slow_next_frame,
    .close = failing_close,
};

/* Returns stereo frames (n, n + 1000) at once, without any pace, at a rate
   whose 300 ms are not a whole number of frames. */
static void *eager_open(const struct aos_audio_source *source, const struct aos_audio_format *wanted,
                        struct aos_audio_format *got) {
    (void)source;
    (void)wanted;
    static int next;
    next = 0;
    got->rate = 11025;
    got->nchannels = 2;
    return &next;
}

static int eager_read(void *handle, short *samples, int frames) {
    int *next = handle;
    for (int i = 0; i < frames; ++i) {
        samples[2 * i] = (short)*next;
        samples[2 * i + 1] = (short)(*next + 1000);
        *next = (*next + 1) % 1000;
    }
    return 0;
}

static void eager_close(void *handle) {
    (void)handle;
}

static const struct aos_audio_source eager_source = {
    .name = "eager",
    .open = eager_open,
    .read = eager_read,
    .close = eager_close,
};

/* ---- the tests ---- */

static void test_video_source(struct aos_file_video *v) {
    struct aos_file_video bad;
    AOS_CHECK(aos_file_video_init(&bad, "RAM:no-such-file.rgb", VIDEO_W, VIDEO_H, VIDEO_FPS) != NULL,
              "video: a missing file is refused");
    AOS_CHECK(aos_file_video_init(&bad, VIDEO_FILE, 100, 100, VIDEO_FPS) != NULL,
              "video: a file smaller than one picture of the size is refused");
    AOS_CHECK(aos_file_video_init(&bad, VIDEO_FILE, 0, VIDEO_H, VIDEO_FPS) != NULL &&
                  aos_file_video_init(&bad, VIDEO_FILE, VIDEO_W, VIDEO_H, 0.0f) != NULL,
              "video: a size or rate out of range is refused");

    const char *error = aos_file_video_init(v, VIDEO_FILE, VIDEO_W, VIDEO_H, VIDEO_FPS);
    AOS_CHECK(error == NULL, "video: the file source is set up");
    if (error != NULL) {
        return;
    }
    AOS_CHECK(strcmp(v->source.driver, "File") == 0 && strcmp(v->source.name, "aos-test-capture.rgb") == 0,
              "video: the source is named after the file");

    struct aos_video_format wanted = {640, 480, 30.0f}, got;
    void *h = v->source.open(&v->source, &wanted, &got);
    AOS_CHECK(h != NULL && got.width == VIDEO_W && got.height == VIDEO_H && got.fps == VIDEO_FPS,
              "video: the source gives the format of the file");
    if (h == NULL) {
        return;
    }
    unsigned char rgb[VIDEO_W * VIDEO_H * 3];
    int order = 1, i = 0;
    long start = aos_test_now_ms();
    for (; i < 5; ++i) {
        order &= v->source.next_frame(h, rgb) == 0 && is_picture(rgb, i % 3);
    }
    long elapsed = aos_test_now_ms() - start;
    AOS_CHECK(order, "video: pictures come in order and again from the start, without the broken rest");
    printf("info: 5 pictures at %.0f per second took %ld ms\n", (double)VIDEO_FPS, elapsed);
    AOS_CHECK(elapsed >= 195 && elapsed < 300, "video: pictures come at the rate of the file");

    /* After a stall of 6 pictures, the next one comes at once and the one
       after it a picture period later: the source does not catch up. */
    aos_test_sleep_ms(300);
    long a = aos_test_now_ms();
    order = v->source.next_frame(h, rgb) == 0 && is_picture(rgb, i++ % 3);
    long b = aos_test_now_ms();
    order &= v->source.next_frame(h, rgb) == 0 && is_picture(rgb, i++ % 3);
    long c = aos_test_now_ms();
    v->source.close(h);
    printf("info: after a stall, pictures came after %ld and %ld ms\n", b - a, c - b);
    AOS_CHECK(order && b - a < 20 && c - b >= 40, "video: a late source skips the lost time");
}

static void test_sound_source(struct aos_file_audio *a) {
    struct aos_file_audio bad;
    AOS_CHECK(aos_file_audio_init(&bad, "RAM:no-such-file.raw", SOUND_RATE, 1) != NULL,
              "sound: a missing file is refused");
    AOS_CHECK(aos_file_audio_init(&bad, SOUND_FILE, 100, 1) != NULL &&
                  aos_file_audio_init(&bad, SOUND_FILE, SOUND_RATE, 3) != NULL,
              "sound: a rate or number of channels out of range is refused");

    const char *error = aos_file_audio_init(a, SOUND_FILE, SOUND_RATE, 1);
    AOS_CHECK(error == NULL, "sound: the file source is set up");
    if (error != NULL) {
        return;
    }
    struct aos_audio_format wanted = {48000, 2}, got;
    void *h = a->source.open(&a->source, &wanted, &got);
    AOS_CHECK(h != NULL && got.rate == SOUND_RATE && got.nchannels == 1, "sound: the source gives the format of the file");
    if (h == NULL) {
        return;
    }
    short samples[400];
    int order = 1, next = 0;
    long start = aos_test_now_ms();
    for (int portion = 0; portion < 4; ++portion) {
        order &= a->source.read(h, samples, 400) == 0;
        for (int i = 0; i < 400; ++i) {
            order &= samples[i] == next;
            next = (next + 1) % SOUND_LEN;
        }
    }
    long elapsed = aos_test_now_ms() - start;
    AOS_CHECK(order, "sound: samples come in order and again from the start, without the broken rest");
    printf("info: 1600 samples at %d Hz took %ld ms\n", SOUND_RATE, elapsed);
    AOS_CHECK(elapsed >= 195 && elapsed < 300, "sound: samples come at the rate of the file");

    /* After a stall of 300 ms, the next portion (50 ms) comes at once and
       the one after it 50 ms later. */
    aos_test_sleep_ms(300);
    long t0 = aos_test_now_ms();
    order = a->source.read(h, samples, 400) == 0 && samples[0] == next;
    long t1 = aos_test_now_ms();
    order &= a->source.read(h, samples, 400) == 0;
    long t2 = aos_test_now_ms();
    a->source.close(h);
    printf("info: after a stall, portions came after %ld and %ld ms\n", t1 - t0, t2 - t1);
    AOS_CHECK(order && t1 - t0 < 20 && t2 - t1 >= 40, "sound: a late source skips the lost time");
}

static MSFilter *camera_reader(MSWebCamManager *manager, const char *id) {
    MSWebCam *cam = ms_web_cam_manager_get_cam(manager, id);
    return cam != NULL ? ms_web_cam_create_reader(cam) : NULL;
}

static void test_camera(MSFactory *factory, struct aos_file_video *v) {
    MSWebCamManager *manager = ms_factory_get_web_cam_manager(factory);
    AOS_CHECK(aos_camera_register(factory, &v->source) == 0, "camera: registered");
    ms_web_cam_manager_reload(manager);
    AOS_CHECK(aos_camera_register(factory, &v->source) == 0, "camera: registering the same source again works");
    int count = 0;
    for (const MSList *it = ms_web_cam_manager_get_list(manager); it != NULL; it = it->next) {
        count += strcmp(ms_web_cam_get_string_id(it->data), "File: aos-test-capture.rgb") == 0;
    }
    AOS_CHECK(count == 1, "camera: listed once as \"File: aos-test-capture.rgb\", also after a reload");
    MSFilter *reader = camera_reader(manager, "File: aos-test-capture.rgb");
    AOS_CHECK(reader != NULL, "camera: its filter is created");
    if (reader == NULL) {
        return;
    }

    MSVideoSize wanted = {640, 480}, size = {0, 0};
    MSPixFmt fmt = MS_PIX_FMT_UNKNOWN;
    ms_filter_call_method(reader, MS_FILTER_SET_VIDEO_SIZE, &wanted);
    ms_filter_call_method(reader, MS_FILTER_GET_VIDEO_SIZE, &size);
    ms_filter_call_method(reader, MS_FILTER_GET_PIX_FMT, &fmt);
    AOS_CHECK(size.width == VIDEO_W - 1 && size.height == VIDEO_H - 1 && fmt == MS_YUV420P,
              "camera: sends YUV420P of the file size cut to even numbers");

    /* The stream asks for half the pictures of the file. */
    float fps = VIDEO_FPS / 2;
    ms_filter_call_method(reader, MS_FILTER_SET_FPS, &fps);
    run(factory, reader, &video_sink_desc, 1000);
    pthread_mutex_lock(&sink.lock);
    printf("info: at %.0f per second the camera sent %d pictures in 1 s, %d of them wrong\n", (double)fps,
           sink.pictures, sink.bad_pictures);
    AOS_CHECK(sink.pictures > 0 && sink.bad_pictures == 0,
              "camera: every picture has the size, colours, row order and orientation of the file");
    /* The first picture of the file goes out at once. */
    AOS_CHECK(sink.first_picture == 0 && sink.pictures_seen == 7, "camera: all pictures of the file come through");
    AOS_CHECK(sink.pictures >= 8 && sink.pictures <= 12, "camera: sends at the picture rate the stream asks for");
    pthread_mutex_unlock(&sink.lock);

    /* The stream takes more than the file gives. */
    fps = 30.0f;
    ms_filter_call_method(reader, MS_FILTER_SET_FPS, &fps);
    run(factory, reader, &video_sink_desc, 1000);
    pthread_mutex_lock(&sink.lock);
    printf("info: at %.0f per second the camera sent %d pictures in 1 s\n", (double)fps, sink.pictures);
    AOS_CHECK(sink.bad_pictures == 0 && sink.pictures >= 18 && sink.pictures <= 22,
              "camera: sends every picture of the source when the stream takes more");
    pthread_mutex_unlock(&sink.lock);
    ms_filter_destroy(reader);

    /* A source that stops: the camera reports 0 pictures per second, which
       makes mediastreamer2 switch to StaticImage. */
    AOS_CHECK(aos_camera_register(factory, &failing_source) == 0, "camera: a second source is registered");
    reader = camera_reader(manager, "Fail: after 3 pictures");
    AOS_CHECK(reader != NULL, "camera: the filter of the second source is created");
    if (reader == NULL) {
        return;
    }
    struct run r;
    float alive = 0.0f, dead = 1.0f;
    start_run(&r, factory, reader, &video_sink_desc, 0);
    aos_test_sleep_ms(100);
    ms_filter_call_method(reader, MS_FILTER_GET_FPS, &alive);
    aos_test_sleep_ms(1400);
    ms_filter_call_method(reader, MS_FILTER_GET_FPS, &dead);
    stop_run(&r);
    printf("info: a failing source: %.1f pictures per second while it works, %.1f after\n", (double)alive,
           (double)dead);
    AOS_CHECK(alive > 0.0f && dead == 0.0f, "camera: reports 0 pictures per second once its source stops");
    ms_filter_destroy(reader);

    /* A slow source is not taken for a stopped one between its pictures,
       but is once it has been silent for four times its pace. */
    AOS_CHECK(aos_camera_register(factory, &slow_source) == 0, "camera: a slow source is registered");
    reader = camera_reader(manager, "Slow: 3 pictures 1.3 s apart");
    AOS_CHECK(reader != NULL, "camera: the filter of the slow source is created");
    if (reader == NULL) {
        return;
    }
    float before = 0.0f, between = 0.0f, stopped = 1.0f;
    start_run(&r, factory, reader, &video_sink_desc, 0);
    aos_test_sleep_ms(1200); /* waiting for the first picture */
    ms_filter_call_method(reader, MS_FILTER_GET_FPS, &before);
    aos_test_sleep_ms(3800); /* pictures at 1.3, 2.6 and 3.9 s: 1.1 s after the last one */
    ms_filter_call_method(reader, MS_FILTER_GET_FPS, &between);
    aos_test_sleep_ms(7000); /* over 8 s after the last one */
    ms_filter_call_method(reader, MS_FILTER_GET_FPS, &stopped);
    stop_run(&r);
    printf("info: a slow source: %.1f pictures per second before its first picture, %.1f between pictures, %.1f "
           "after it stops\n",
           (double)before, (double)between, (double)stopped);
    AOS_CHECK(before > 0.0f, "camera: a source gets more than 1 s for its first picture");
    AOS_CHECK(between > 0.0f && stopped == 0.0f,
              "camera: a source slower than one picture a second stays alive until silent for four times its pace");
    ms_filter_destroy(reader);
}

static void test_capture(MSFactory *factory, struct aos_file_audio *a) {
    MSSndCardManager *manager = ms_factory_get_snd_card_manager(factory);
    aos_ahi_register(factory, &a->source);
    MSSndCard *card = ms_snd_card_manager_get_card(manager, "AHI: default unit");
    AOS_CHECK(card != NULL && (ms_snd_card_get_capabilities(card) & MS_SND_CARD_CAP_CAPTURE) &&
                  (ms_snd_card_get_capabilities(card) & MS_SND_CARD_CAP_PLAYBACK),
              "card: plays and captures with a capture source");
    MSFilter *reader = card != NULL ? ms_snd_card_create_reader(card) : NULL;
    AOS_CHECK(reader != NULL, "card: its capture filter is created");
    if (reader == NULL) {
        return;
    }
    int rate = 48000, nchannels = 2;
    ms_filter_call_method(reader, MS_FILTER_SET_SAMPLE_RATE, &rate);
    ms_filter_call_method(reader, MS_FILTER_SET_NCHANNELS, &nchannels);
    rate = nchannels = 0;
    ms_filter_call_method(reader, MS_FILTER_GET_SAMPLE_RATE, &rate);
    ms_filter_call_method(reader, MS_FILTER_GET_NCHANNELS, &nchannels);
    AOS_CHECK(rate == SOUND_RATE && nchannels == 1, "card: the capture filter reports the format of the source");

    run(factory, reader, &sound_sink_desc, 500);
    pthread_mutex_lock(&sink.lock);
    printf("info: the capture filter sent %ld samples in 500 ms\n", sink.frames);
    AOS_CHECK(sink.frames > 0 && sink.bad_frames == 0, "card: captured samples follow the file without a gap");
    AOS_CHECK(sink.frames >= 3200 && sink.frames <= 4400, "card: captured samples come at the rate of the source");
    pthread_mutex_unlock(&sink.lock);
    ms_filter_destroy(reader);

    /* A source that returns at once fills the queue, which drops the oldest
       sound. The reader thread pauses then, so this test ends instead of
       hanging the system, and the drop keeps whole frames. */
    MSFactory *eager = ms_factory_new_with_voip();
    aos_ahi_register(eager, &eager_source);
    card = ms_snd_card_manager_get_card(ms_factory_get_snd_card_manager(eager), "AHI: default unit");
    reader = card != NULL ? ms_snd_card_create_reader(card) : NULL;
    AOS_CHECK(reader != NULL, "card: the capture filter of a source without pace is created");
    if (reader != NULL) {
        struct run r;
        start_run(&r, eager, reader, &sound_sink_desc, 1);
        aos_test_sleep_ms(500);
        stop_run(&r);
        pthread_mutex_lock(&sink.lock);
        printf("info: a source without pace: %ld frames in 500 ms, %d broken\n", sink.frames, sink.bad_frames);
        AOS_CHECK(sink.frames > 0 && sink.bad_frames == 0,
                  "card: dropping sound from a source that runs ahead keeps whole frames");
        pthread_mutex_unlock(&sink.lock);
        ms_filter_destroy(reader);
    }
    ms_factory_destroy(eager);

    MSFactory *plain = ms_factory_new_with_voip();
    aos_ahi_register(plain, NULL);
    card = ms_snd_card_manager_get_card(ms_factory_get_snd_card_manager(plain), "AHI: default unit");
    AOS_CHECK(card != NULL && ms_snd_card_get_capabilities(card) == MS_SND_CARD_CAP_PLAYBACK &&
                  ms_snd_card_create_reader(card) == NULL,
              "card: only plays without a capture source");
    ms_factory_destroy(plain);
}

int main(void) {
    pthread_mutex_init(&sink.lock, NULL);
    AOS_CHECK(write_files() == 0, "the test files are written");
    static struct aos_file_video video;
    static struct aos_file_audio sound;
    test_video_source(&video);
    test_sound_source(&sound);

    MSFactory *factory = ms_factory_new_with_voip();
    if (video.source.open != NULL) {
        test_camera(factory, &video);
    }
    if (sound.source.open != NULL) {
        test_capture(factory, &sound);
    }
    ms_factory_destroy(factory);
    remove(VIDEO_FILE);
    remove(SOUND_FILE);
    pthread_mutex_destroy(&sink.lock);
    return aos_test_result();
}
