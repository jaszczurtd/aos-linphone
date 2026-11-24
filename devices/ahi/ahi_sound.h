/*
 * ahi_sound - an AHI sound card for mediastreamer2 on AROS.
 *
 * The card plays sound through the default unit of ahi.device; it does not
 * capture yet. Each playback filter has its own writer thread, which opens
 * the device and keeps two linked CMD_WRITE requests of 40 ms going.
 */
#ifndef AOS_AHI_SOUND_H
#define AOS_AHI_SOUND_H

#include <mediastreamer2/msfactory.h>

/* Registers the card with the sound card manager of the factory and
   detects it. Call it before the sound devices are (re)loaded. */
void aos_ahi_register(MSFactory *factory);

#endif
