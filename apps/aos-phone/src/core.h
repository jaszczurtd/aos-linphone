/*
 * core - the linphone core in its own thread.
 *
 * The thread started by aos_core_start() creates the linphone core, runs
 * linphone_core_iterate() and makes every liblinphone call; no other file of
 * the program includes the liblinphone headers. The user interface talks to
 * it through two queues:
 *
 *   - the request functions below copy a command into the command queue,
 *     which the core thread empties on every turn of its loop (about every
 *     20 ms); they never block and return -1 when the queue is full;
 *   - the core thread reports what happened as events in the event queue
 *     and calls the notify function of the configuration after each one;
 *     the interface takes them with aos_core_next_event().
 *
 * Text is UTF-8 in both directions. Calls carry an id assigned by the core
 * thread, unique for the run of the program.
 */
#ifndef AOS_PHONE_CORE_H
#define AOS_PHONE_CORE_H

struct aos_video_source;
struct aos_audio_source;

#define AOS_CORE_ADDR_MAX  256  /* bytes of a SIP address, with the NUL */
#define AOS_CORE_TEXT_MAX  1024 /* bytes of a message or reason, with the NUL */
#define AOS_CORE_MAX_CALLS 8    /* calls at a time; the core refuses more */

enum aos_reg_state {
    AOS_REG_NONE,
    AOS_REG_PROGRESS,
    AOS_REG_OK,
    AOS_REG_CLEARED,
    AOS_REG_FAILED,
    /* A REGISTER renews the registration: still valid when it was
       registered before, a new attempt after a failure otherwise. */
    AOS_REG_REFRESHING
};

enum aos_call_state {
    AOS_CALL_INCOMING,  /* waiting to be answered */
    AOS_CALL_OUTGOING,  /* being set up */
    AOS_CALL_RINGING,   /* ringing at the other end */
    AOS_CALL_CONNECTED, /* media flowing */
    AOS_CALL_PAUSED,
    AOS_CALL_ENDED, /* last event of the call */
    AOS_CALL_FAILED /* last event of the call */
};

enum aos_msg_state {
    AOS_MSG_SENDING,
    AOS_MSG_DELIVERED,
    AOS_MSG_FAILED
};

enum aos_event_type {
    /* The core runs. id: its SIP UDP port, peer: identity of the default
       account, text: its server address; both empty without an account.
       AOS_EVENT_ACCOUNTS with all accounts follows. */
    AOS_EVENT_STARTED,
    /* The core could not be created; the thread has ended. */
    AOS_EVENT_START_FAILED,
    /* Registration state of an account. peer: identity, state, text:
       reason, id: 1 when calls go out through this account (the default
       one). */
    AOS_EVENT_ACCOUNT,
    /* The list of accounts follows: the receiver forgets the accounts it
       knows. id: how many AOS_EVENT_ACCOUNT_INFO events come. Sent after
       the core starts and after every change of the accounts. */
    AOS_EVENT_ACCOUNTS,
    /* One account of the list. peer: identity, text: server, state, id: 1
       for the default account. */
    AOS_EVENT_ACCOUNT_INFO,
    /* Call state, or video starting or stopping in a connected call. id,
       state, peer: the other party, text: reason, video: the call is
       connected and has video (known once the media streams run). */
    AOS_EVENT_CALL,
    /* Sound levels of a connected call in dBm0, about four times a
       second. id, play_db (received), record_db (sent). */
    AOS_EVENT_LEVELS,
    /* A text message arrived. peer: sender, text: the message. */
    AOS_EVENT_MESSAGE,
    /* Delivery state of a sent message. id: as returned by
       aos_core_send_message(), state, peer. */
    AOS_EVENT_MESSAGE_STATE,
    /* A connected call is being recorded. id, text: the file, in the
       system code page. */
    AOS_EVENT_RECORDING,
    /* A snapshot of the received video was requested. id: the call, text:
       the JPEG file, in the system code page. */
    AOS_EVENT_SNAPSHOT,
    /* A request could not be carried out. text: what failed, peer: the
       identity given in a request about an account, else empty. */
    AOS_EVENT_ERROR,
    /* id events were dropped because the event queue stayed full. */
    AOS_EVENT_LOST
};

struct aos_core_event {
    enum aos_event_type type;
    int id;
    int state;
    int video;
    float play_db;
    float record_db;
    char peer[AOS_CORE_ADDR_MAX];
    char text[AOS_CORE_TEXT_MAX];
};

struct aos_core_config {
    const char *data_dir;      /* configuration and data of the core */
    const char *resources_dir; /* holds belr/grammars and sounds/linphone */
    int sip_port;              /* UDP port for SIP; 0: a random one */
    /* Directory where each call is recorded to a WAV file, both sides
       mixed; NULL or "": no recording. */
    const char *record_dir;
    /* Native window id for the video display: the surface of
       devices/video/video_display.h that shows the pictures. */
    void *video_window;
    /* Camera to send video from: a part of its id, e.g. "Mire" or
       "StaticImage"; NULL or "": the first camera of uvc.class (VHI), or
       when there is none, the camera the core chose. */
    const char *camera;
    /* Source of one more camera (devices/source.h), e.g. a file; NULL:
       none. It must stay valid until aos_core_stop() returns. */
    const struct aos_video_source *extra_camera;
    /* Source of the sound the AHI card captures; NULL: the card only
       plays and calls send silence. Valid as long as extra_camera. */
    const struct aos_audio_source *capture;
    /* Nonzero: the captured sound holds no echo of what the card plays,
       as with a file, so calls run without the echo canceller, which
       would only take time and drop samples. Not saved in linphonerc. */
    int capture_without_echo;
    /* STUN server for ICE, e.g. "stun.linphone.org": calls then gather
       their public address and connectivity candidates, which helps
       through NAT. NULL or "": no ICE. */
    const char *stun_server;
    /* Nonzero: liblinphone logs messages, not only warnings and errors. */
    int verbose;
    /* Called in the core thread after each new event. */
    void (*notify)(void *context);
    void *context;
};

/* Starts the core thread. Returns 0, or -1 when the thread cannot be
   started; whether the core itself came up arrives as AOS_EVENT_STARTED or
   AOS_EVENT_START_FAILED. */
int aos_core_start(const struct aos_core_config *config);

/* Ends the calls, stops the core and waits for the thread to end. */
void aos_core_stop(void);

/* Takes the oldest event. Returns 1, or 0 when there is none. */
int aos_core_next_event(struct aos_core_event *event);

/* Adds the account with this identity, or updates the one there is, and
   registers it; it becomes the default account, which calls go out
   through. server is the registrar, e.g. sip:10.8.0.1:55555; NULL or ""
   means the identity's domain. An empty password keeps the saved one of an
   existing account. An existing account registers again at once, also
   with nothing changed, e.g. after a failure. Accounts stay in linphonerc
   until removed. */
int aos_core_register(const char *identity, const char *password, const char *server);

/* Stops registering the account with this identity (NULL or "": the
   default one); it stays saved. */
int aos_core_unregister(const char *identity);

/* Removes the account with this identity and its password from the core
   and linphonerc; a registered account is unregistered first. */
int aos_core_remove_account(const char *identity);

/* Makes the account with this identity the default one. */
int aos_core_set_default_account(const char *identity);

/* Calls a SIP address, or a user name in the account's domain. */
int aos_core_call(const char *address);

/* Answers the call, or the first incoming call when call_id is 0. */
int aos_core_answer(int call_id);

/* Declines an incoming call or ends any other; call_id 0 means the current
   call. */
int aos_core_hangup(int call_id);

/* Plays a WAV file to the other party of a connected call, mixed into
   what the call sends; a file already playing stops. call_id 0 means the
   current call. file is a path in the system code page. Fails (as an
   AOS_EVENT_ERROR) once the sound of the call has been restarted, e.g.
   after a pause, because liblinphone keeps a player for the first sound
   stream of the call only. */
int aos_core_play(int call_id, const char *file);

/* Saves the next picture received in a call with video to a JPEG file;
   call_id 0 means the current call. file is a path in the system code
   page. */
int aos_core_snapshot(int call_id, const char *file);

/* Turns the video of a connected call on (on nonzero) or off, by updating
   the call; call_id 0 means the current call. The other side may refuse
   the picture; the AOS_EVENT_CALL that follows tells whether the call has
   video. Fails (as an AOS_EVENT_ERROR) when the call is not connected. */
int aos_core_set_video(int call_id, int on);

/* Sends a text message. Returns its id (1 or more) for the
   AOS_EVENT_MESSAGE_STATE events, or -1. */
int aos_core_send_message(const char *peer, const char *text);

#endif
