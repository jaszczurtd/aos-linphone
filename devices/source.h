/*
 * source - where the camera and the microphone of the device layer take
 * their data from.
 *
 * The camera (video/camera.h) and the capture part of the AHI sound card
 * (ahi/ahi_sound.h) each run a thread that pulls pictures or samples from
 * a source and hands them to mediastreamer2. A source only opens itself
 * for a stream, waits for the next picture or portion of samples, and
 * closes. Files (files/file_source.h) are one kind of source; a real camera
 * or microphone is another, so the devices stay the same when the source
 * changes.
 *
 * A source is a structure of the program that must stay valid as long as
 * the factories its device is registered with. open() runs again for each
 * stream, and the handle it returns belongs to that stream. The device
 * calls the functions of one handle from one thread at a time.
 */
#ifndef AOS_SOURCE_H
#define AOS_SOURCE_H

struct aos_video_format {
    int width;  /* pixels */
    int height; /* pixels */
    float fps;  /* pictures per second */
};

struct aos_video_source {
    /* The camera id is "<driver>: <name>", e.g. "File: clip.rgb". */
    const char *driver;
    const char *name;
    /* Opens the source for a stream. wanted is the format the stream asks
       for; the source writes into got the format it delivers, which may
       differ. Returns a handle, or NULL when the source cannot deliver. */
    void *(*open)(const struct aos_video_source *source, const struct aos_video_format *wanted,
                  struct aos_video_format *got);
    /* Waits for the next picture and copies it into rgb: got.width x
       got.height pixels of 3 bytes in the order red, green, blue, rows from
       top to bottom without padding. Returns 0; 1 when this attempt gave no
       picture (e.g. a frame broken on the way) and the device should try
       again; -1 when no picture comes any more. It returns within about one
       picture period, or soon after a timeout of the hardware. */
    int (*next_frame)(void *handle, unsigned char *rgb);
    void (*close)(void *handle);
    void *data; /* for the source itself */
};

struct aos_audio_format {
    int rate;      /* sample frames per second */
    int nchannels; /* 1 or 2 */
};

struct aos_audio_source {
    const char *name;
    /* As for video: wanted is what the stream asks for, got what the
       source delivers. Returns a handle, or NULL. */
    void *(*open)(const struct aos_audio_source *source, const struct aos_audio_format *wanted,
                  struct aos_audio_format *got);
    /* Waits until frames sample frames have been captured and copies them
       into samples: signed 16-bit values in the byte order of the
       processor, channels interleaved. Returns 0, or -1 when no samples
       come any more. It returns about when the last of them was captured,
       so a portion of 20 ms takes about 20 ms. */
    int (*read)(void *handle, short *samples, int frames);
    void (*close)(void *handle);
    void *data;
};

#endif
