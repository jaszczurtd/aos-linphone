/*
 * camera - a camera for mediastreamer2 that takes its pictures from a video
 * source (source.h).
 *
 * The filter runs a thread of its own while its stream runs. The thread
 * waits for each picture of the source, converts it from RGB24 to YUV420P
 * and keeps the newest one; the filter, in the MSTicker thread, sends each
 * new picture on, at most at the picture rate the stream asks for. A
 * picture of odd width or height loses its last column or row. When the
 * source stops giving pictures, the filter reports 0 pictures per second,
 * and mediastreamer2 replaces the camera with StaticImage after 5 s. A
 * source counts as stopped when its first picture has not come 10 s after
 * the start, or later after 1 s without a picture, or after four times the
 * longest wait it has kept the stream waiting before (at most 30 s), so a
 * slow camera is not taken for a dead one.
 *
 * The filter converts the pictures itself because mediastreamer2 built
 * with libyuv reads its RGB24 formats unlike their names: MS_RGB24 as
 * blue, green, red into full-range YUV, and MS_RGB24_REV as red, green,
 * blue stored from the bottom row up.
 */
#ifndef AOS_CAMERA_H
#define AOS_CAMERA_H

#include "source.h"

#include <mediastreamer2/msfactory.h>

#define AOS_CAMERA_MAX 4 /* different sources a program can register */

/* Adds a camera that shows the pictures of source to the cameras of the
   factory, also after the video devices are reloaded. Its id is
   "<driver>: <name>" of the source. The source must stay valid as long as
   the factory. Returns 0, or -1 when AOS_CAMERA_MAX other sources are
   registered already. */
int aos_camera_register(MSFactory *factory, const struct aos_video_source *source);

#endif
