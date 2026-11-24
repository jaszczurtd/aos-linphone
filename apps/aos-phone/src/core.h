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

#define AOS_CORE_ADDR_MAX  256  /* bytes of a SIP address, with the NUL */
#define AOS_CORE_TEXT_MAX  1024 /* bytes of a message or reason, with the NUL */
#define AOS_CORE_MAX_CALLS 8    /* calls at a time; the core refuses more */

enum aos_reg_state {
    AOS_REG_NONE,
    AOS_REG_PROGRESS,
    AOS_REG_OK,
    AOS_REG_CLEARED,
    AOS_REG_FAILED
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
    /* The core runs. id: its SIP UDP port, peer: identity of the saved
       account, text: its server address; both empty without a saved
       account. */
    AOS_EVENT_STARTED,
    /* The core could not be created; the thread has ended. */
    AOS_EVENT_START_FAILED,
    /* Registration state of the account. peer: identity, text: reason. */
    AOS_EVENT_ACCOUNT,
    /* Call state. id, state, peer: the other party, text: reason. */
    AOS_EVENT_CALL,
    /* Sound levels of a connected call in dBm0, about four times a
       second. id, play_db (received), record_db (sent). */
    AOS_EVENT_LEVELS,
    /* A text message arrived. peer: sender, text: the message. */
    AOS_EVENT_MESSAGE,
    /* Delivery state of a sent message. id: as returned by
       aos_core_send_message(), state, peer. */
    AOS_EVENT_MESSAGE_STATE,
    /* A request could not be carried out. text: what failed. */
    AOS_EVENT_ERROR,
    /* id events were dropped because the event queue stayed full. */
    AOS_EVENT_LOST
};

struct aos_core_event {
    enum aos_event_type type;
    int id;
    int state;
    float play_db;
    float record_db;
    char peer[AOS_CORE_ADDR_MAX];
    char text[AOS_CORE_TEXT_MAX];
};

struct aos_core_config {
    const char *data_dir;      /* configuration and data of the core */
    const char *resources_dir; /* holds belr/grammars */
    int sip_port;              /* UDP port for SIP; 0: a random one */
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

/* Replaces the account with this one and registers it. server is the
   registrar, e.g. sip:10.8.0.1:55555; NULL or "" means the identity's
   domain. */
int aos_core_register(const char *identity, const char *password, const char *server);

int aos_core_unregister(void);

/* Calls a SIP address, or a user name in the account's domain. */
int aos_core_call(const char *address);

/* Answers the call, or the first incoming call when call_id is 0. */
int aos_core_answer(int call_id);

/* Declines an incoming call or ends any other; call_id 0 means the current
   call. */
int aos_core_hangup(int call_id);

/* Sends a text message. Returns its id (1 or more) for the
   AOS_EVENT_MESSAGE_STATE events, or -1. */
int aos_core_send_message(const char *peer, const char *text);

#endif
