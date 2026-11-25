/*
 * vhi_camera - video sources for the cameras uvc.class publishes as VHI
 * libraries (uvc0.vhi, uvc1.vhi, ...).
 *
 * Each source grabs pictures through the VHI library of its camera: one
 * VHI_DIGITIZE_PICTURE per picture, which waits for the next frame and
 * returns it as RGB24 (red, green, blue), converted from the camera's
 * format by uvc.class. A source picks the uncompressed (YUY2) mode nearest
 * to the size the stream asks for, at the mode's own picture rate; MJPEG
 * modes are left out, because uvc.class rejects about every second frame
 * of some cameras in them. A frame lost on the way only skips that picture.
 */
#ifndef AOS_VHI_CAMERA_H
#define AOS_VHI_CAMERA_H

#include <mediastreamer2/msfactory.h>

#define AOS_VHI_UNITS 4 /* uvc0.vhi to uvc3.vhi are looked for */

/* Adds a camera "VHI: <card name> (uvc<n>)" to the factory for each VHI
   library of uvc.class present now. Returns how many it added. */
int aos_vhi_cameras_register(MSFactory *factory);

#endif
