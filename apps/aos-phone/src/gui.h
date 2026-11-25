/*
 * gui - the Zune interface of aos-phone: the windows, their actions, the
 * ARexx port and the handling of core events.
 *
 * Everything here runs in the task that called aos_gui_create(), except
 * aos_gui_notify(), which the core thread calls.
 */
#ifndef AOS_PHONE_GUI_H
#define AOS_PHONE_GUI_H

/* Creates the application and opens the main window. Returns 0, or -1. */
int aos_gui_create(void);

/* Runs the interface until the user quits or the task gets Ctrl-C. */
void aos_gui_run(void);

/* Closes the windows and frees the interface; the core must be stopped. */
void aos_gui_dispose(void);

/* The notify function for struct aos_core_config: wakes the interface
   task, which then takes the new core events. */
void aos_gui_notify(void *context);

/* The video surface the call window shows, for the video_window of struct
   aos_core_config. It stays valid until aos_gui_dispose(). */
void *aos_gui_video_window(void);

#endif
