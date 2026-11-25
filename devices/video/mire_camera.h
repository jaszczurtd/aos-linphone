/*
 * mire_camera - the Mire test camera of mediastreamer2, always listed.
 *
 * mediastreamer2 builds Mire (a synthetic moving picture) into every video
 * build but lists it only when the environment variable DEBUG is 1. This
 * module lists it without that, for calls with a moving test picture.
 */
#ifndef AOS_MIRE_CAMERA_H
#define AOS_MIRE_CAMERA_H

#include <mediastreamer2/msfactory.h>

/* Adds Mire to the cameras of the factory, also after the video devices
   are reloaded. Its id is "Mire: Mire (synthetic moving picture)". */
void aos_mire_register(MSFactory *factory);

#endif
