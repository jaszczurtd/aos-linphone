/*
 * The lamp class: a small round light in one of three colours, for the
 * state of the SIP account next to its name in the main window, and as a
 * ringing lamp with animated waves for an incoming call.
 */
#ifndef AOS_PHONE_LAMP_H
#define AOS_PHONE_LAMP_H

#include <intuition/classusr.h>
#include <libraries/mui.h>
#include <utility/tagitem.h>

enum aos_lamp_colour {
    AOS_LAMP_RED,    /* not registered: no account, no network, refused */
    AOS_LAMP_YELLOW, /* registering */
    AOS_LAMP_GREEN   /* registered */
};

/* Attribute (init, set) with the colour, an enum aos_lamp_colour; the
   default is AOS_LAMP_RED. */
#define AOS_LAMP_COLOUR (TAG_USER | 0x414c0001)

/* Attribute (init) TRUE for a ringing lamp: twice the size, a smaller light
   with waves spreading from it, for an incoming call. */
#define AOS_LAMP_WAVES (TAG_USER | 0x414c0002)

/* Attribute (set) with the frame of the waves, 0 to AOS_LAMP_FRAMES - 1;
   setting the next frame every 100-150 ms animates them. */
#define AOS_LAMP_FRAME  (TAG_USER | 0x414c0003)
#define AOS_LAMP_FRAMES 8

/* The class, or NULL. Lamps are made with
   NewObject(lamp_class->mcc_Class, NULL, AOS_LAMP_COLOUR, ..., TAG_DONE). */
struct MUI_CustomClass *aos_lamp_class_create(void);
void aos_lamp_class_delete(struct MUI_CustomClass *lamp_class);

#endif
