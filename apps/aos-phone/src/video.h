/*
 * video - a Zune area that shows one view of a video surface.
 *
 * The area tells the surface its size while it is shown, so that pictures
 * come scaled to fit it, and 0 x 0 while it is hidden, so that the display
 * filter drops them. It draws the newest picture centred and fills the rest
 * of its box with black. Everything here runs in the interface task.
 */
#ifndef AOS_PHONE_VIDEO_H
#define AOS_PHONE_VIDEO_H

#include "video/video_display.h"

#include <intuition/classusr.h>
#include <libraries/mui.h>

/* Creates the class. Returns NULL on failure. */
struct MUI_CustomClass *aos_video_class_create(void);

void aos_video_class_delete(struct MUI_CustomClass *video_class);

/* Creates an area of the class for one view of the surface. */
Object *aos_video_area_new(struct MUI_CustomClass *video_class, struct aos_video_surface *surface,
                           enum aos_video_view view);

/* Draws the newest picture of the area's view, when the area is shown. */
void aos_video_area_update(Object *area);

#endif
