/*
 * video_display - a video display for mediastreamer2 that hands the
 * pictures to the user interface.
 *
 * The display filter AOS_VIDEO_DISPLAY_NAME takes the remote picture on
 * input 0 and the local preview on input 1. It scales each picture to fit
 * the size the interface gives for its view, keeping the proportions, and
 * converts it to ARGB in the MSTicker thread. The pictures go to a surface
 * that the interface creates and reads; the interface never calls
 * mediastreamer2, and the filter never calls the interface except through
 * the notify function.
 *
 * The filter finds its surface through the native window id of
 * mediastreamer2: pass the surface to linphone_core_set_native_video_window_id()
 * and the filter name to linphone_core_set_video_display_filter().
 *
 * A surface can be read from any task or thread; the C library mutexes it
 * uses work outside pthread threads too.
 */
#ifndef AOS_VIDEO_DISPLAY_H
#define AOS_VIDEO_DISPLAY_H

#include <mediastreamer2/msfactory.h>

#define AOS_VIDEO_DISPLAY_NAME "AosVideoDisplay"

enum aos_video_view {
    AOS_VIDEO_REMOTE,  /* the picture of the other party */
    AOS_VIDEO_PREVIEW, /* the picture this side sends */
    AOS_VIDEO_VIEWS
};

/* A picture ready to draw: width x height pixels of 4 bytes each, in the
   order alpha, red, green, blue (RECTFMT_ARGB of cybergraphics). */
struct aos_video_frame {
    int width;
    int height;
    int stride; /* bytes from one row to the next */
    const unsigned char *pixels;
};

struct aos_video_surface;

/* Called in an MSTicker thread when a view has a new picture or has lost
   its picture; it must not block. */
typedef void (*aos_video_notify_fn)(void *context);

/* Creates a surface whose views are not shown yet (size 0 x 0). notify may
   be NULL. Returns NULL when out of memory. */
struct aos_video_surface *aos_video_surface_create(aos_video_notify_fn notify, void *context);

/* Frees the surface. No display filter may use it any more: the calls have
   ended, or the core has stopped. */
void aos_video_surface_destroy(struct aos_video_surface *surface);

/* Sets the size of the area the view is drawn in. Pictures then fit in it,
   with their proportions kept. 0 x 0 hides the view: its pictures are
   dropped before conversion. */
void aos_video_surface_set_size(struct aos_video_surface *surface, enum aos_video_view view, int width, int height);

/* Takes the newest picture of the view. Returns 1 and fills *frame, whose
   pixels stay valid and unchanged until the next call for the same view, or
   0 when the view has no picture (no video, or the stream has ended). The
   picture can be smaller than the area of the view, and larger for a while
   after the area shrinks. */
int aos_video_surface_get_frame(struct aos_video_surface *surface, enum aos_video_view view,
                                struct aos_video_frame *frame);

/* Registers the display filter with the factory. Call it once, after the
   core is created. */
void aos_video_display_register(MSFactory *factory);

#endif
