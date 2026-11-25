/*
 * Raw files as camera and microphone sources: the file is read one picture
 * or one portion at a time, and each read waits until the moment the
 * picture or the last sample of the portion is due.
 */
#include "files/file_source.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_DIMENSION 4096
#define MAX_FPS       60.0f
#define MIN_RATE      4000
#define MAX_RATE      96000
#define AUDIO_LATE_NS 100000000LL /* sound later than this skips the lost time */

/* ---- time ---- */

static int64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static void sleep_until(int64_t due) {
    for (int64_t left = due - now_ns(); left > 0; left = due - now_ns()) {
        struct timespec ts = {(time_t)(left / 1000000000LL), (long)(left % 1000000000LL)};
        nanosleep(&ts, NULL);
    }
}

/* ---- files ---- */

/* The file part of an AmigaDOS or POSIX path. */
static const char *file_part(const char *path) {
    const char *name = path;
    for (const char *p = path; *p != '\0'; ++p) {
        if (*p == ':' || *p == '/') {
            name = p + 1;
        }
    }
    return name;
}

/* Opens path for reading and gives its size in bytes, or returns NULL. */
static FILE *open_sized(const char *path, long *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (*size = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    return file;
}

/* Reads count records of record_size bytes, starting again at the
   beginning of the file at its end. Returns 0, or -1 when the file gives
   nothing even from its beginning. */
static int read_looped(FILE *file, void *buffer, size_t record_size, size_t count) {
    unsigned char *p = buffer;
    int rewound = 0;
    while (count > 0) {
        size_t got = fread(p, record_size, count, file);
        p += got * record_size;
        count -= got;
        if (count > 0) {
            if (got == 0 && rewound) {
                return -1;
            }
            rewound = got == 0;
            clearerr(file);
            if (fseek(file, 0, SEEK_SET) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static const char *copy_path(char *path, char *name, const char *from) {
    if (from == NULL || from[0] == '\0') {
        return "no file given";
    }
    if (strlen(from) >= AOS_FILE_PATH_MAX) {
        return "the file path is too long";
    }
    strcpy(path, from);
    /* The name only labels the source: a long one is cut. */
    const char *part = file_part(path);
    size_t len = strlen(part) < AOS_FILE_NAME_MAX - 1 ? strlen(part) : AOS_FILE_NAME_MAX - 1;
    memcpy(name, part, len);
    name[len] = '\0';
    return NULL;
}

/* ---- video ---- */

struct video_handle {
    FILE *file;
    size_t frame_bytes;
    int64_t start;   /* when picture 0 of the current pace was due */
    int64_t emitted; /* pictures delivered since start */
    double period;   /* ns */
};

static void *video_open(const struct aos_video_source *source, const struct aos_video_format *wanted,
                        struct aos_video_format *got) {
    (void)wanted; /* a file has one format */
    const struct aos_file_video *v = source->data;
    struct video_handle *h = calloc(1, sizeof(*h));
    long size = 0;
    if (h == NULL) {
        return NULL;
    }
    h->frame_bytes = (size_t)v->format.width * (size_t)v->format.height * 3;
    h->file = open_sized(v->path, &size);
    if (h->file == NULL || (size_t)size < h->frame_bytes) {
        if (h->file != NULL) {
            fclose(h->file);
        }
        free(h);
        return NULL;
    }
    h->period = 1e9 / v->format.fps;
    h->start = now_ns();
    *got = v->format;
    return h;
}

static int video_next_frame(void *handle, unsigned char *rgb) {
    struct video_handle *h = handle;
    int64_t due = h->start + (int64_t)((double)h->emitted * h->period);
    int64_t now = now_ns();
    if (now < due) {
        sleep_until(due);
    } else if ((double)(now - due) > 2 * h->period) {
        /* Late by more than two pictures: go on from now. */
        h->start = now;
        h->emitted = 0;
    }
    /* A broken rest after the last whole picture reads as no picture, and
       reading goes on from the start. */
    if (read_looped(h->file, rgb, h->frame_bytes, 1) != 0) {
        return -1;
    }
    ++h->emitted;
    return 0;
}

static void video_close(void *handle) {
    struct video_handle *h = handle;
    fclose(h->file);
    free(h);
}

const char *aos_file_video_init(struct aos_file_video *v, const char *path, int width, int height, float fps) {
    memset(v, 0, sizeof(*v));
    if (width < 2 || height < 2 || width > MAX_DIMENSION || height > MAX_DIMENSION) {
        return "the picture size is out of range";
    }
    if (!(fps > 0.0f && fps <= MAX_FPS)) {
        return "the picture rate is out of range";
    }
    const char *error = copy_path(v->path, v->name, path);
    if (error != NULL) {
        return error;
    }
    long size = 0;
    FILE *file = open_sized(v->path, &size);
    if (file == NULL) {
        return "the video file cannot be opened";
    }
    fclose(file);
    if ((size_t)size < (size_t)width * (size_t)height * 3) {
        return "the video file holds no whole picture of that size";
    }
    v->format.width = width;
    v->format.height = height;
    v->format.fps = fps;
    v->source.driver = "File";
    v->source.name = v->name;
    v->source.open = video_open;
    v->source.next_frame = video_next_frame;
    v->source.close = video_close;
    v->source.data = v;
    return NULL;
}

/* ---- sound ---- */

struct audio_handle {
    FILE *file;
    int rate;
    int nchannels;
    int64_t start;   /* when sample frame 0 of the current pace was captured */
    int64_t emitted; /* sample frames delivered since start */
};

static void *audio_open(const struct aos_audio_source *source, const struct aos_audio_format *wanted,
                        struct aos_audio_format *got) {
    (void)wanted;
    const struct aos_file_audio *a = source->data;
    struct audio_handle *h = calloc(1, sizeof(*h));
    long size = 0;
    if (h == NULL) {
        return NULL;
    }
    h->file = open_sized(a->path, &size);
    if (h->file == NULL || size < 2L * a->format.nchannels) {
        if (h->file != NULL) {
            fclose(h->file);
        }
        free(h);
        return NULL;
    }
    h->rate = a->format.rate;
    h->nchannels = a->format.nchannels;
    h->start = now_ns();
    *got = a->format;
    return h;
}

/* Nanoseconds of n sample frames, without overflow in long runs. */
static int64_t frames_ns(int64_t n, int rate) {
    return n / rate * 1000000000LL + n % rate * 1000000000LL / rate;
}

static int audio_read(void *handle, short *samples, int frames) {
    struct audio_handle *h = handle;
    int64_t due = h->start + frames_ns(h->emitted + frames, h->rate);
    int64_t now = now_ns();
    if (now < due) {
        sleep_until(due);
    } else if (now - due > AUDIO_LATE_NS) {
        /* Skip the lost time: these samples count as captured now. */
        h->start = now;
        h->emitted = -frames;
    }
    if (read_looped(h->file, samples, sizeof(short) * (size_t)h->nchannels, (size_t)frames) != 0) {
        return -1;
    }
    h->emitted += frames;
    return 0;
}

static void audio_close(void *handle) {
    struct audio_handle *h = handle;
    fclose(h->file);
    free(h);
}

const char *aos_file_audio_init(struct aos_file_audio *a, const char *path, int rate, int nchannels) {
    memset(a, 0, sizeof(*a));
    if (rate < MIN_RATE || rate > MAX_RATE) {
        return "the sample rate is out of range";
    }
    if (nchannels != 1 && nchannels != 2) {
        return "the number of channels must be 1 or 2";
    }
    const char *error = copy_path(a->path, a->name, path);
    if (error != NULL) {
        return error;
    }
    long size = 0;
    FILE *file = open_sized(a->path, &size);
    if (file == NULL) {
        return "the sound file cannot be opened";
    }
    fclose(file);
    if (size < 2L * nchannels) {
        return "the sound file holds no whole sample frame";
    }
    a->format.rate = rate;
    a->format.nchannels = nchannels;
    a->source.name = a->name;
    a->source.open = audio_open;
    a->source.read = audio_read;
    a->source.close = audio_close;
    a->source.data = a;
    return NULL;
}
