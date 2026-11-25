/*
 * ahi_sound - an AHI sound card for mediastreamer2 on AROS.
 *
 * The card plays sound through the default unit of ahi.device. Each
 * playback filter has its own writer thread, which opens the device and
 * keeps two linked CMD_WRITE requests of 40 ms going.
 *
 * Given a sound source (source.h), the card also captures: each capture
 * filter has its own reader thread, which takes portions of 20 ms from the
 * source at the rate and number of channels the source delivers, and
 * mediastreamer2 resamples them for the call. Without a source the card
 * only plays.
 */
#ifndef AOS_AHI_SOUND_H
#define AOS_AHI_SOUND_H

#include "source.h"

#include <mediastreamer2/msfactory.h>

/* Registers the card with the sound card manager of the factory and
   detects it. Call it before the sound devices are (re)loaded. capture is
   the source of the captured sound, or NULL; it must stay valid as long as
   the factory. The source applies to the cards detected from then on, also
   in other factories: a program registers one. */
void aos_ahi_register(MSFactory *factory, const struct aos_audio_source *capture);

#endif
