/*
 * ahi_source - a sound source (source.h) that records from a unit of
 * ahi.device, the microphone or line input chosen in the AHI preferences.
 *
 * The source runs a capture thread of its own, which holds the message
 * port and the I/O request: it reads portions of 20 ms with CMD_READ,
 * continuing at the io_Offset of the request before, so ahi.device records
 * without gaps, and queues them for read(). It records 48 kHz mono; AHI
 * converts from the frequency of the unit, and mediastreamer2 resamples for
 * the codec of the call.
 */
#ifndef AOS_AHI_SOURCE_H
#define AOS_AHI_SOURCE_H

#include "source.h"

struct aos_ahi_source {
    struct aos_audio_source source; /* what aos_ahi_register() takes */
    unsigned long unit;
    char name[32];
};

/* Sets up s to record from AHI unit `unit` (0-3). */
void aos_ahi_source_init(struct aos_ahi_source *s, unsigned long unit);

#endif
