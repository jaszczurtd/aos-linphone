/*
 * file_source - sources of the device layer that play raw files in a loop,
 * paced like a camera and a microphone.
 *
 * The files have no header, container or codec:
 *
 *   - video: pictures of 3 bytes per pixel in the order red, green, blue,
 *     rows from top to bottom, one picture after another. Bytes after the
 *     last whole picture are ignored.
 *   - sound: signed 16-bit samples in the byte order of the processor,
 *     channels interleaved.
 *
 * The format of a file is given when the source is set up. A source
 * delivers the pictures or samples at the pace of the format, measured on
 * the monotonic clock, and starts again at the beginning of the file when
 * it reaches the end. When the device falls behind, the source skips the
 * time it lost instead of catching up in a burst.
 */
#ifndef AOS_FILE_SOURCE_H
#define AOS_FILE_SOURCE_H

#include "source.h"

#define AOS_FILE_PATH_MAX 256
#define AOS_FILE_NAME_MAX 64

struct aos_file_video {
    struct aos_video_source source; /* what aos_camera_register() takes */
    char path[AOS_FILE_PATH_MAX];
    char name[AOS_FILE_NAME_MAX]; /* the file part of the path */
    struct aos_video_format format;
};

struct aos_file_audio {
    struct aos_audio_source source; /* what aos_ahi_register() takes */
    char path[AOS_FILE_PATH_MAX];
    char name[AOS_FILE_NAME_MAX];
    struct aos_audio_format format;
};

/* Sets up v to play the file at path: pictures of width x height pixels,
   fps of them per second. The camera id becomes "File: <file name>".
   Returns NULL, or a message saying what is wrong: a format out of range,
   a path that is too long, a file that cannot be opened or holds no whole
   picture. v must stay valid while a device uses it. */
const char *aos_file_video_init(struct aos_file_video *v, const char *path, int width, int height, float fps);

/* Sets up a to play the file at path: rate sample frames per second of
   nchannels channels (1 or 2). Returns NULL, or a message as above. */
const char *aos_file_audio_init(struct aos_file_audio *a, const char *path, int rate, int nchannels);

#endif
