/*
 * The linphone core thread. This is the only file of aos-phone that uses
 * liblinphone; every function that calls it starts with CORE_THREAD_ONLY(),
 * which stops the program when it runs in any other thread.
 */
#include "core.h"
#include "queue.h"

#include "ahi/ahi_sound.h"
#include "video/camera.h"
#include "video/mire_camera.h"
#include "video/video_display.h"
#include "vhi/vhi_camera.h"

#include "aos/net.h"

#include <linphone/core.h>
#include <linphone/factory.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CORE_STACK_SIZE       (1024 * 1024)
#define CORE_TICK_US          20000
#define LEVELS_INTERVAL_MS    250
#define EVENT_WAIT_MS         2000 /* longest wait for room in a full event queue */
#define SHUTDOWN_MS           2000 /* time given to calls to end before the core stops */
#define EVENT_CAPACITY        64
#define COMMAND_CAPACITY      16
#define MAX_MESSAGES          16 /* sent messages waiting for their final state */
#define PASSWORD_MAX          128
#define PATH_MAX_LEN          256
#define MAX_LEAVING           4   /* removed accounts still unregistering */
#define DEFAULT_DOWNLOAD_KBIT 512 /* the other side then sends VP8 at VGA size or less */

enum command_type {
    CORE_CMD_REGISTER,
    CORE_CMD_UNREGISTER,
    CORE_CMD_REMOVE_ACCOUNT,
    CORE_CMD_SET_DEFAULT,
    CORE_CMD_CALL,
    CORE_CMD_ANSWER,
    CORE_CMD_HANGUP,
    CORE_CMD_PLAY,
    CORE_CMD_SNAPSHOT,
    CORE_CMD_VIDEO,
    CORE_CMD_MESSAGE,
    CORE_CMD_STOP
};

struct command {
    enum command_type type;
    int id;
    int flag;                        /* CORE_CMD_VIDEO: on or off */
    char address[AOS_CORE_ADDR_MAX]; /* identity, callee or message peer */
    char server[AOS_CORE_ADDR_MAX];
    char password[PASSWORD_MAX];
    char text[AOS_CORE_TEXT_MAX]; /* message, file to play or snapshot file */
};

struct call_slot {
    int id; /* 0: free */
    int state;
    LinphoneCall *call;
    int video;       /* video ran when the streams last started; 0 when not connected */
    int runs;        /* times the call entered StreamsRunning */
    int player_runs; /* runs when the call player was first taken; 0: never */
};

struct message_slot {
    int id; /* 0: free */
    LinphoneChatMessage *message;
    char peer[AOS_CORE_ADDR_MAX];
};

static struct {
    /* Shared by both threads. */
    struct aos_queue commands;
    struct aos_queue events;
    struct command command_storage[COMMAND_CAPACITY];
    struct aos_core_event event_storage[EVENT_CAPACITY];
    pthread_mutex_t lock; /* guards finished */
    int finished;         /* the core thread has left its loop */

    /* Interface side. */
    pthread_t thread;
    int started;
    int next_message_id;

    /* Set once before the thread starts. */
    struct aos_core_config config;
    char data_dir[PATH_MAX_LEN];
    char resources_dir[PATH_MAX_LEN];
    char record_dir[PATH_MAX_LEN];
    char camera[AOS_CORE_ADDR_MAX];

    /* Core thread only. */
    pthread_t self;
    LinphoneFactory *factory;
    LinphoneCore *core;
    struct call_slot calls[AOS_CORE_MAX_CALLS];
    struct message_slot messages[MAX_MESSAGES];
    /* Removed accounts whose password stays until their unregistration has
       ended, since unregistering needs it; user@domain, "" when free. */
    char leaving[MAX_LEAVING][AOS_CORE_ADDR_MAX];
    int next_call_id;
    int recordings;  /* recording files named so far */
    int vhi_cameras; /* cameras of uvc.class found at start */
    unsigned lost;
    int stop;
} g;

static void core_thread_only(const char *where) {
    if (!pthread_equal(pthread_self(), g.self)) {
        fprintf(stderr, "aos-phone: %s() called outside the core thread\n", where);
        abort();
    }
}

#define CORE_THREAD_ONLY() core_thread_only(__func__)

static void copy_text(char *dst, size_t size, const char *src) {
    snprintf(dst, size, "%s", src != NULL ? src : "");
}

/* Copies UTF-8 text, cut at a character boundary when it does not fit. */
static void copy_utf8(char *dst, size_t size, const char *src) {
    size_t len = src != NULL ? strlen(src) : 0;
    if (len >= size) {
        len = size - 1;
        while (len > 0 && ((unsigned char)src[len] & 0xc0) == 0x80) {
            --len;
        }
    }
    if (len > 0) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

static long elapsed_ms(const struct timespec *since) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long)(now.tv_sec - since->tv_sec) * 1000 + (now.tv_nsec - since->tv_nsec) / 1000000;
}

/* ---- events ---- */

static void event_init(struct aos_core_event *event, enum aos_event_type type) {
    memset(event, 0, sizeof(*event));
    event->type = type;
}

static void copy_address(char *dst, size_t size, const LinphoneAddress *address) {
    CORE_THREAD_ONLY();
    if (address == NULL) {
        dst[0] = '\0';
        return;
    }
    char *text = linphone_address_as_string_uri_only(address);
    copy_text(dst, size, text);
    bctbx_free(text);
}

static int try_push(const struct aos_core_event *event, int wait) {
    int waited = 0;
    while (aos_queue_push(&g.events, event) != 0) {
        if (!wait || waited >= EVENT_WAIT_MS) {
            return -1;
        }
        g.config.notify(g.config.context);
        usleep(10000);
        waited += 10;
    }
    return 0;
}

/* Queues an event for the interface. A full queue gets EVENT_WAIT_MS to
   drain; after that, events are counted as lost, without waiting, until
   an AOS_EVENT_LOST fits in again. */
static void push_event(const struct aos_core_event *event) {
    CORE_THREAD_ONLY();
    if (g.lost > 0) {
        struct aos_core_event lost;
        event_init(&lost, AOS_EVENT_LOST);
        lost.id = (int)g.lost;
        if (try_push(&lost, 0) == 0) {
            g.lost = 0;
        }
    }
    if (g.lost > 0 || try_push(event, 1) != 0) {
        ++g.lost;
    }
    g.config.notify(g.config.context);
}

static void push_error(const char *what) {
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_ERROR);
    copy_text(event.text, sizeof(event.text), what);
    push_event(&event);
}

/* An error of a request about an account, with the identity as requested. */
static void push_account_error(const char *identity, const char *what) {
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_ERROR);
    copy_text(event.peer, sizeof(event.peer), identity);
    copy_text(event.text, sizeof(event.text), what);
    push_event(&event);
}

/* ---- calls ---- */

static struct call_slot *find_call(LinphoneCall *call) {
    for (int i = 0; i < AOS_CORE_MAX_CALLS; ++i) {
        if (g.calls[i].id != 0 && g.calls[i].call == call) {
            return &g.calls[i];
        }
    }
    return NULL;
}

static struct call_slot *find_call_id(int id) {
    for (int i = 0; i < AOS_CORE_MAX_CALLS; ++i) {
        if (g.calls[i].id != 0 && g.calls[i].id == id) {
            return &g.calls[i];
        }
    }
    return NULL;
}

static struct call_slot *add_call(LinphoneCall *call) {
    CORE_THREAD_ONLY();
    for (int i = 0; i < AOS_CORE_MAX_CALLS; ++i) {
        if (g.calls[i].id == 0) {
            g.calls[i].id = ++g.next_call_id;
            g.calls[i].state = -1;
            g.calls[i].call = linphone_call_ref(call);
            return &g.calls[i];
        }
    }
    return NULL;
}

static void free_call(struct call_slot *slot) {
    CORE_THREAD_ONLY();
    linphone_call_unref(slot->call);
    memset(slot, 0, sizeof(*slot));
}

/* Returns the reported state, or -1 for states the interface does not see. */
static int map_call_state(LinphoneCallState state) {
    switch (state) {
    case LinphoneCallStateIncomingReceived:
    case LinphoneCallStatePushIncomingReceived:
    case LinphoneCallStateIncomingEarlyMedia:
        return AOS_CALL_INCOMING;
    case LinphoneCallStateOutgoingInit:
    case LinphoneCallStateOutgoingProgress:
        return AOS_CALL_OUTGOING;
    case LinphoneCallStateOutgoingRinging:
    case LinphoneCallStateOutgoingEarlyMedia:
        return AOS_CALL_RINGING;
    case LinphoneCallStateConnected:
    case LinphoneCallStateStreamsRunning:
    case LinphoneCallStateUpdating:
    case LinphoneCallStateUpdatedByRemote:
    case LinphoneCallStateResuming:
        return AOS_CALL_CONNECTED;
    case LinphoneCallStatePausing:
    case LinphoneCallStatePaused:
    case LinphoneCallStatePausedByRemote:
        return AOS_CALL_PAUSED;
    case LinphoneCallStateEnd:
        return AOS_CALL_ENDED;
    case LinphoneCallStateError:
        return AOS_CALL_FAILED;
    default:
        return -1;
    }
}

/* Writes dir and name joined as an AmigaDOS path: no '/' after a volume
   or assign name ("RAM:"), whose parent "RAM:/" does not exist. */
static void join_path(char *dst, size_t size, const char *dir, const char *name) {
    copy_text(dst, size, dir);
    size_t n = strlen(dst);
    if (n > 0 && dst[n - 1] != ':' && dst[n - 1] != '/' && n + 1 < size) {
        dst[n++] = '/';
        dst[n] = '\0';
    }
    copy_text(dst + n, size - n, name);
}

/* Parameters to start or answer a call with: they name a new recording
   file when the configuration gives a directory for recordings. */
static LinphoneCallParams *create_params(LinphoneCall *call) {
    CORE_THREAD_ONLY();
    LinphoneCallParams *params = linphone_core_create_call_params(g.core, call);
    if (params != NULL && g.record_dir[0] != '\0') {
        time_t now = time(NULL);
        struct tm local;
        char stamp[16], name[48], path[PATH_MAX_LEN + 48];
        localtime_r(&now, &local);
        strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
        snprintf(name, sizeof(name), "call-%s-%d.wav", stamp, ++g.recordings);
        join_path(path, sizeof(path), g.record_dir, name);
        linphone_call_params_set_record_file(params, path);
    }
    return params;
}

/* Starts recording a call that has just connected, when its parameters
   name a file. liblinphone resumes the recording by itself after a restart
   of the sound stream and stops it when the call ends. */
static void start_recording(struct call_slot *slot) {
    CORE_THREAD_ONLY();
    /* Copy the name first: the string belongs to the call parameters, which
       starting the recording may replace. */
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_RECORDING);
    event.id = slot->id;
    copy_text(event.text, sizeof(event.text),
              linphone_call_params_get_record_file(linphone_call_get_params(slot->call)));
    if (event.text[0] == '\0') {
        return;
    }
    linphone_call_start_recording(slot->call);
    /* The recorder creates the file when it starts. */
    if (access(event.text, F_OK) != 0) {
        push_error("the call could not be recorded");
        return;
    }
    push_event(&event);
}

static void on_call_state(LinphoneCore *core, LinphoneCall *call, LinphoneCallState state, const char *message) {
    (void)core;
    CORE_THREAD_ONLY();
    struct call_slot *slot = find_call(call);
    if (slot == NULL) {
        /* A call joins the table in its first state; the later states of a
           refused call are ignored. */
        if (state != LinphoneCallStateIncomingReceived && state != LinphoneCallStatePushIncomingReceived &&
            state != LinphoneCallStateOutgoingInit) {
            return;
        }
        slot = add_call(call);
        if (slot == NULL) {
            push_error("too many calls; the new one is refused");
            if (state != LinphoneCallStateOutgoingInit) {
                linphone_call_decline(call, LinphoneReasonBusy);
            } else {
                linphone_call_terminate(call);
            }
            return;
        }
    }

    int reported = map_call_state(state);
    /* Video can come and go each time the streams start again, and runs
       only while the call is connected. */
    int video = slot->video;
    if (state == LinphoneCallStateStreamsRunning) {
        video = linphone_call_params_video_enabled(linphone_call_get_current_params(call)) != 0;
    } else if (reported != AOS_CALL_CONNECTED) {
        video = 0;
    }
    if (reported >= 0 && (reported != slot->state || video != slot->video)) {
        /* End and Error are final for the interface; Released follows. */
        slot->state = reported;
        slot->video = video;
        struct aos_core_event event;
        event_init(&event, AOS_EVENT_CALL);
        event.id = slot->id;
        event.state = reported;
        event.video = video;
        copy_address(event.peer, sizeof(event.peer), linphone_call_get_remote_address(call));
        copy_text(event.text, sizeof(event.text), message);
        push_event(&event);
    }
    if (state == LinphoneCallStateStreamsRunning && ++slot->runs == 1) {
        start_recording(slot);
    }
    if (state == LinphoneCallStateReleased) {
        free_call(slot);
    }
}

static void report_levels(void) {
    CORE_THREAD_ONLY();
    for (int i = 0; i < AOS_CORE_MAX_CALLS; ++i) {
        struct call_slot *slot = &g.calls[i];
        if (slot->id != 0 && slot->state == AOS_CALL_CONNECTED) {
            struct aos_core_event event;
            event_init(&event, AOS_EVENT_LEVELS);
            event.id = slot->id;
            event.play_db = linphone_call_get_play_volume(slot->call);
            event.record_db = linphone_call_get_record_volume(slot->call);
            push_event(&event);
        }
    }
}

/* ---- account ---- */

static int map_reg_state(LinphoneRegistrationState state) {
    switch (state) {
    case LinphoneRegistrationProgress: return AOS_REG_PROGRESS;
    case LinphoneRegistrationOk: return AOS_REG_OK;
    case LinphoneRegistrationRefreshing: return AOS_REG_REFRESHING;
    case LinphoneRegistrationCleared: return AOS_REG_CLEARED;
    case LinphoneRegistrationFailed: return AOS_REG_FAILED;
    default: return AOS_REG_NONE;
    }
}

static void leaving_key(const LinphoneAddress *identity, char *key, size_t size) {
    snprintf(key, size, "%s@%s", linphone_address_get_username(identity) ? linphone_address_get_username(identity) : "",
             linphone_address_get_domain(identity) ? linphone_address_get_domain(identity) : "");
}

static void remove_password(const char *key) {
    CORE_THREAD_ONLY();
    char username[AOS_CORE_ADDR_MAX];
    copy_text(username, sizeof(username), key);
    char *at = strrchr(username, '@');
    if (at == NULL) {
        return;
    }
    *at = '\0';
    const LinphoneAuthInfo *auth = linphone_core_find_auth_info(g.core, NULL, username, at + 1);
    if (auth != NULL) {
        linphone_core_remove_auth_info(g.core, auth);
        linphone_config_sync(linphone_core_get_config(g.core));
    }
}

/* Drops the password of a removed account once its registration has
   ended; accounts still in the list keep theirs. */
static void forget_leaving(const LinphoneAddress *identity) {
    CORE_THREAD_ONLY();
    if (identity == NULL) {
        return;
    }
    char key[AOS_CORE_ADDR_MAX];
    leaving_key(identity, key, sizeof(key));
    for (int i = 0; i < MAX_LEAVING; ++i) {
        if (g.leaving[i][0] != '\0' && strcmp(g.leaving[i], key) == 0) {
            remove_password(key);
            g.leaving[i][0] = '\0';
        }
    }
}

static void on_account_state(LinphoneCore *core, LinphoneAccount *account, LinphoneRegistrationState state,
                             const char *message) {
    (void)core;
    CORE_THREAD_ONLY();
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_ACCOUNT);
    event.state = map_reg_state(state);
    event.id = account == linphone_core_get_default_account(g.core);
    copy_address(event.peer, sizeof(event.peer),
                 linphone_account_params_get_identity_address(linphone_account_get_params(account)));
    copy_text(event.text, sizeof(event.text), message);
    push_event(&event);
    if (state != LinphoneRegistrationProgress && state != LinphoneRegistrationOk &&
        state != LinphoneRegistrationRefreshing) {
        forget_leaving(linphone_account_params_get_identity_address(linphone_account_get_params(account)));
    }
}

/* The account whose identity matches the address (user, domain, port), or
   NULL. */
static LinphoneAccount *find_account(const char *identity) {
    CORE_THREAD_ONLY();
    if (identity == NULL || identity[0] == '\0') {
        return linphone_core_get_default_account(g.core);
    }
    LinphoneAddress *wanted = linphone_factory_create_address(g.factory, identity);
    if (wanted == NULL) {
        return NULL;
    }
    LinphoneAccount *found = NULL;
    for (const bctbx_list_t *it = linphone_core_get_account_list(g.core); it != NULL && found == NULL;
         it = bctbx_list_next(it)) {
        LinphoneAccount *account = bctbx_list_get_data(it);
        const LinphoneAddress *address = linphone_account_params_get_identity_address(linphone_account_get_params(account));
        if (address != NULL && linphone_address_weak_equal(address, wanted)) {
            found = account;
        }
    }
    linphone_address_unref(wanted);
    return found;
}

/* Sends the whole list of accounts (AOS_EVENT_ACCOUNTS, then one
   AOS_EVENT_ACCOUNT_INFO each) and writes linphonerc, so that a change of
   the accounts survives whatever happens next. */
static void push_accounts(void) {
    CORE_THREAD_ONLY();
    const bctbx_list_t *list = linphone_core_get_account_list(g.core);
    LinphoneAccount *def = linphone_core_get_default_account(g.core);
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_ACCOUNTS);
    event.id = (int)bctbx_list_size(list);
    push_event(&event);
    for (const bctbx_list_t *it = list; it != NULL; it = bctbx_list_next(it)) {
        LinphoneAccount *account = bctbx_list_get_data(it);
        const LinphoneAccountParams *params = linphone_account_get_params(account);
        event_init(&event, AOS_EVENT_ACCOUNT_INFO);
        event.id = account == def;
        event.state = map_reg_state(linphone_account_get_state(account));
        copy_address(event.peer, sizeof(event.peer), linphone_account_params_get_identity_address(params));
        copy_address(event.text, sizeof(event.text), linphone_account_params_get_server_address(params));
        push_event(&event);
    }
    linphone_config_sync(linphone_core_get_config(g.core));
}

/* Replaces the password saved for user@domain. */
static void set_password(const char *username, const char *domain, const char *password) {
    CORE_THREAD_ONLY();
    const LinphoneAuthInfo *old = linphone_core_find_auth_info(g.core, NULL, username, domain);
    if (old != NULL) {
        linphone_core_remove_auth_info(g.core, old);
    }
    LinphoneAuthInfo *auth =
        linphone_factory_create_auth_info_2(g.factory, username, NULL, password, NULL, NULL, domain, NULL);
    linphone_core_add_auth_info(g.core, auth);
    linphone_auth_info_unref(auth);
}

static void do_register(const struct command *cmd) {
    CORE_THREAD_ONLY();
    LinphoneAddress *identity = linphone_factory_create_address(g.factory, cmd->address);
    if (identity == NULL || linphone_address_get_username(identity) == NULL) {
        push_account_error(cmd->address, "invalid identity");
        if (identity != NULL) {
            linphone_address_unref(identity);
        }
        return;
    }
    char server_text[AOS_CORE_ADDR_MAX + 8];
    if (cmd->server[0] != '\0') {
        copy_text(server_text, sizeof(server_text), cmd->server);
    } else {
        snprintf(server_text, sizeof(server_text), "sip:%s", linphone_address_get_domain(identity));
    }
    LinphoneAddress *server = linphone_factory_create_address(g.factory, server_text);
    if (server == NULL) {
        push_account_error(cmd->address, "invalid server address");
        linphone_address_unref(identity);
        return;
    }

    LinphoneAccount *account = find_account(cmd->address);
    if (account == NULL && cmd->password[0] == '\0') {
        push_account_error(cmd->address, "a new account needs a password");
    } else {
        if (cmd->password[0] != '\0') {
            set_password(linphone_address_get_username(identity), linphone_address_get_domain(identity),
                         cmd->password);
        }
        if (account != NULL) {
            const LinphoneAccountParams *old = linphone_account_get_params(account);
            const LinphoneAddress *old_server = linphone_account_params_get_server_address(old);
            /* A new server or a registration turned back on makes the core
               register by itself; otherwise the parameters stay the same
               and only a refresh sends a REGISTER, with a new password or
               again after a failure. */
            int refresh = linphone_account_params_register_enabled(old) && old_server != NULL &&
                          linphone_address_equal(old_server, server);
            LinphoneAccountParams *params = linphone_account_params_clone(old);
            linphone_account_params_set_server_address(params, server);
            linphone_account_params_enable_register(params, TRUE);
            linphone_account_set_params(account, params);
            linphone_account_params_unref(params);
            if (refresh) {
                linphone_account_refresh_register(account);
            }
        } else {
            LinphoneAccountParams *params = linphone_core_create_account_params(g.core);
            linphone_account_params_set_identity_address(params, identity);
            linphone_account_params_set_server_address(params, server);
            linphone_account_params_enable_register(params, TRUE);
            account = linphone_core_create_account(g.core, params);
            linphone_account_params_unref(params);
            linphone_core_add_account(g.core, account);
            linphone_account_unref(account);
        }
        linphone_core_set_default_account(g.core, account);
        push_accounts();
    }
    linphone_address_unref(identity);
    linphone_address_unref(server);
}

static void do_unregister(const struct command *cmd) {
    CORE_THREAD_ONLY();
    LinphoneAccount *account = find_account(cmd->address);
    if (account == NULL) {
        push_account_error(cmd->address, "no such account to unregister");
        return;
    }
    LinphoneAccountParams *params = linphone_account_params_clone(linphone_account_get_params(account));
    linphone_account_params_enable_register(params, FALSE);
    linphone_account_set_params(account, params);
    linphone_account_params_unref(params);
    push_accounts();
}

static void do_remove_account(const struct command *cmd) {
    CORE_THREAD_ONLY();
    LinphoneAccount *account = cmd->address[0] != '\0' ? find_account(cmd->address) : NULL;
    if (account == NULL) {
        push_account_error(cmd->address, "no such account to remove");
        return;
    }
    char key[AOS_CORE_ADDR_MAX];
    leaving_key(linphone_account_params_get_identity_address(linphone_account_get_params(account)), key, sizeof(key));
    int registered = linphone_account_get_state(account) == LinphoneRegistrationOk ||
                     linphone_account_get_state(account) == LinphoneRegistrationRefreshing ||
                     linphone_account_get_state(account) == LinphoneRegistrationProgress;
    /* The core unregisters a registered account before it lets it go, and
       needs the password for that: it goes when the registration ends
       (on_account_state()), or when the core stops. */
    linphone_core_remove_account(g.core, account);
    int queued = 0;
    for (int i = 0; registered && !queued && i < MAX_LEAVING; ++i) {
        if (g.leaving[i][0] == '\0') {
            copy_text(g.leaving[i], sizeof(g.leaving[i]), key);
            queued = 1;
        }
    }
    if (!queued) {
        remove_password(key);
    }
    if (linphone_core_get_default_account(g.core) == NULL && linphone_core_get_account_list(g.core) != NULL) {
        linphone_core_set_default_account(g.core, bctbx_list_get_data(linphone_core_get_account_list(g.core)));
    }
    push_accounts();
}

static void do_set_default(const struct command *cmd) {
    CORE_THREAD_ONLY();
    LinphoneAccount *account = cmd->address[0] != '\0' ? find_account(cmd->address) : NULL;
    if (account == NULL) {
        push_account_error(cmd->address, "no such account");
        return;
    }
    linphone_core_set_default_account(g.core, account);
    push_accounts();
}

/* ---- call requests ---- */

static void do_call(const struct command *cmd) {
    CORE_THREAD_ONLY();
    LinphoneAddress *address = linphone_core_interpret_url_2(g.core, cmd->address, FALSE);
    if (address == NULL) {
        push_error("invalid address to call");
        return;
    }
    LinphoneCallParams *params = create_params(NULL);
    if (params == NULL || linphone_core_invite_address_with_params(g.core, address, params) == NULL) {
        push_error("the call could not be started");
    }
    if (params != NULL) {
        linphone_call_params_unref(params);
    }
    linphone_address_unref(address);
}

static struct call_slot *first_call_in(int state) {
    for (int i = 0; i < AOS_CORE_MAX_CALLS; ++i) {
        if (g.calls[i].id != 0 && (state < 0 || g.calls[i].state == state)) {
            return &g.calls[i];
        }
    }
    return NULL;
}

static void do_answer(int id) {
    CORE_THREAD_ONLY();
    struct call_slot *slot = id != 0 ? find_call_id(id) : first_call_in(AOS_CALL_INCOMING);
    if (slot == NULL || slot->state != AOS_CALL_INCOMING) {
        push_error("no incoming call to answer");
        return;
    }
    LinphoneCallParams *params = create_params(slot->call);
    if (params == NULL || linphone_call_accept_with_params(slot->call, params) != 0) {
        push_error("the call could not be answered");
    }
    if (params != NULL) {
        linphone_call_params_unref(params);
    }
}

static void do_hangup(int id) {
    CORE_THREAD_ONLY();
    struct call_slot *slot = NULL;
    if (id != 0) {
        slot = find_call_id(id);
    } else {
        LinphoneCall *current = linphone_core_get_current_call(g.core);
        slot = current != NULL ? find_call(current) : first_call_in(-1);
    }
    if (slot == NULL) {
        push_error("no call to hang up");
        return;
    }
    if (slot->state == AOS_CALL_INCOMING) {
        linphone_call_decline(slot->call, LinphoneReasonDeclined);
    } else {
        linphone_call_terminate(slot->call);
    }
}

static void do_play(const struct command *cmd) {
    CORE_THREAD_ONLY();
    struct call_slot *slot = NULL;
    if (cmd->id != 0) {
        slot = find_call_id(cmd->id);
    } else {
        LinphoneCall *current = linphone_core_get_current_call(g.core);
        slot = current != NULL ? find_call(current) : NULL;
    }
    if (slot == NULL || linphone_call_get_state(slot->call) != LinphoneCallStateStreamsRunning) {
        push_error("no connected call to play the file to");
        return;
    }
    /* liblinphone creates the call player once, on the sound stream of that
       moment, and keeps it when a restart of the sound replaces the stream:
       the player must not be touched after that. */
    if (slot->player_runs == 0) {
        slot->player_runs = slot->runs;
    }
    if (slot->player_runs != slot->runs) {
        push_error("the sound of the call was restarted; files cannot be played to it any more");
        return;
    }
    LinphonePlayer *player = linphone_call_get_player(slot->call);
    /* Opening closes the file played before. */
    if (player == NULL || linphone_player_open(player, cmd->text) != 0 || linphone_player_start(player) != 0) {
        push_error("the file could not be played to the call");
    }
}

static void do_snapshot(const struct command *cmd) {
    CORE_THREAD_ONLY();
    struct call_slot *slot = NULL;
    if (cmd->id != 0) {
        slot = find_call_id(cmd->id);
    } else {
        LinphoneCall *current = linphone_core_get_current_call(g.core);
        slot = current != NULL ? find_call(current) : NULL;
    }
    if (slot == NULL || !slot->video) {
        push_error("no call with video to take a snapshot of");
        return;
    }
    /* The picture is written by the video stream, after this returns. */
    if (linphone_call_take_video_snapshot(slot->call, cmd->text) != 0) {
        push_error("the snapshot could not be taken");
        return;
    }
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_SNAPSHOT);
    event.id = slot->id;
    copy_text(event.text, sizeof(event.text), cmd->text);
    push_event(&event);
}

static void do_set_video(const struct command *cmd) {
    CORE_THREAD_ONLY();
    struct call_slot *slot = NULL;
    if (cmd->id != 0) {
        slot = find_call_id(cmd->id);
    } else {
        LinphoneCall *current = linphone_core_get_current_call(g.core);
        slot = current != NULL ? find_call(current) : NULL;
    }
    if (slot == NULL || linphone_call_get_state(slot->call) != LinphoneCallStateStreamsRunning) {
        push_error("no connected call to change the video of");
        return;
    }
    /* A re-INVITE with or without the video stream; the call goes through
       Updating to StreamsRunning, whose event reports the outcome. */
    LinphoneCallParams *params = linphone_core_create_call_params(g.core, slot->call);
    if (params == NULL) {
        push_error("the video of the call could not be changed");
        return;
    }
    linphone_call_params_enable_video(params, cmd->flag ? TRUE : FALSE);
    if (linphone_call_update(slot->call, params) != 0) {
        push_error("the video of the call could not be changed");
    }
    linphone_call_params_unref(params);
}

/* ---- messages ---- */

static int map_msg_state(LinphoneChatMessageState state) {
    switch (state) {
    case LinphoneChatMessageStateInProgress: return AOS_MSG_SENDING;
    case LinphoneChatMessageStateDelivered:
    case LinphoneChatMessageStateDeliveredToUser:
    case LinphoneChatMessageStateDisplayed:
        return AOS_MSG_DELIVERED;
    case LinphoneChatMessageStateNotDelivered:
    case LinphoneChatMessageStateFileTransferError:
        return AOS_MSG_FAILED;
    default: return -1;
    }
}

static void push_message_state(int id, int state, const char *peer) {
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_MESSAGE_STATE);
    event.id = id;
    event.state = state;
    copy_text(event.peer, sizeof(event.peer), peer);
    push_event(&event);
}

static void on_message_state(LinphoneChatMessage *message, LinphoneChatMessageState state) {
    CORE_THREAD_ONLY();
    struct message_slot *slot = NULL;
    for (int i = 0; i < MAX_MESSAGES; ++i) {
        if (g.messages[i].id != 0 && g.messages[i].message == message) {
            slot = &g.messages[i];
        }
    }
    int reported = map_msg_state(state);
    if (slot == NULL || reported < 0) {
        return;
    }
    push_message_state(slot->id, reported, slot->peer);
    if (reported != AOS_MSG_SENDING) {
        linphone_chat_message_unref(slot->message);
        memset(slot, 0, sizeof(*slot));
    }
}

static void on_message_received(LinphoneCore *core, LinphoneChatRoom *room, LinphoneChatMessage *message) {
    (void)core;
    (void)room;
    CORE_THREAD_ONLY();
    const char *text = linphone_chat_message_get_utf8_text(message);
    if (text == NULL) {
        return; /* no text part */
    }
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_MESSAGE);
    copy_address(event.peer, sizeof(event.peer), linphone_chat_message_get_from_address(message));
    copy_utf8(event.text, sizeof(event.text), text);
    push_event(&event);
}

static LinphoneChatRoom *basic_chat_room(const LinphoneAddress *peer) {
    CORE_THREAD_ONLY();
    LinphoneConferenceParams *params = linphone_core_create_conference_params_2(g.core, NULL);
    linphone_conference_params_enable_chat(params, TRUE);
    linphone_conference_params_enable_group(params, FALSE);
    linphone_chat_params_set_backend(linphone_conference_params_get_chat_params(params),
                                     LinphoneChatRoomBackendBasic);
    bctbx_list_t *participants = bctbx_list_append(NULL, (void *)peer);
    LinphoneChatRoom *room = linphone_core_create_chat_room_7(g.core, params, participants);
    bctbx_list_free(participants);
    linphone_conference_params_unref(params);
    return room;
}

static void do_message(const struct command *cmd) {
    CORE_THREAD_ONLY();
    struct message_slot *slot = NULL;
    for (int i = 0; i < MAX_MESSAGES && slot == NULL; ++i) {
        if (g.messages[i].id == 0) {
            slot = &g.messages[i];
        }
    }
    LinphoneAddress *peer = linphone_core_interpret_url_2(g.core, cmd->address, FALSE);
    LinphoneChatRoom *room = peer != NULL && slot != NULL ? basic_chat_room(peer) : NULL;
    if (room == NULL) {
        push_error(slot == NULL   ? "too many messages waiting for delivery"
                   : peer == NULL ? "invalid message address"
                                  : "no chat room for the message");
        push_message_state(cmd->id, AOS_MSG_FAILED, cmd->address);
        if (peer != NULL) {
            linphone_address_unref(peer);
        }
        return;
    }

    LinphoneChatMessage *message = linphone_chat_room_create_message_from_utf8(room, cmd->text);
    LinphoneChatMessageCbs *cbs = linphone_factory_create_chat_message_cbs(g.factory);
    linphone_chat_message_cbs_set_msg_state_changed(cbs, on_message_state);
    linphone_chat_message_add_callbacks(message, cbs);
    linphone_chat_message_cbs_unref(cbs);

    slot->id = cmd->id;
    slot->message = message; /* our reference, dropped at the final state */
    copy_address(slot->peer, sizeof(slot->peer), peer);
    linphone_chat_message_send(message);

    linphone_chat_room_unref(room);
    linphone_address_unref(peer);
}

/* ---- the thread ---- */

static void push_started(void) {
    CORE_THREAD_ONLY();
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_STARTED);
    LinphoneTransports *used = linphone_core_get_transports_used(g.core);
    event.id = linphone_transports_get_udp_port(used);
    linphone_transports_unref(used);
    LinphoneAccount *account = linphone_core_get_default_account(g.core);
    if (account != NULL) {
        const LinphoneAccountParams *params = linphone_account_get_params(account);
        copy_address(event.peer, sizeof(event.peer), linphone_account_params_get_identity_address(params));
        copy_address(event.text, sizeof(event.text), linphone_account_params_get_server_address(params));
    }
    push_event(&event);
    push_accounts();
}

/* The core chooses its sound devices before the AHI card exists. Reloading
   them makes the card the default playback device, and the capture device
   when it captures, but keeps the empty list of ring devices: when no
   device rings, ring on the first one that plays. */
static void choose_ring_device(void) {
    CORE_THREAD_ONLY();
    bctbx_list_t *devices = linphone_core_get_extended_audio_devices(g.core);
    LinphoneAudioDevice *player = NULL;
    int ringing = 0;
    for (const bctbx_list_t *it = devices; it != NULL; it = bctbx_list_next(it)) {
        LinphoneAudioDevice *device = bctbx_list_get_data(it);
        ringing |= linphone_audio_device_get_use_for_ringing(device);
        if (player == NULL && linphone_audio_device_has_capability(device, LinphoneAudioDeviceCapabilityPlay)) {
            player = device;
        }
    }
    if (!ringing && player != NULL) {
        linphone_audio_device_set_use_for_ringing(player, TRUE);
    }
    bctbx_list_free_with_data(devices, (bctbx_list_free_func)linphone_audio_device_unref);
}

/* Sends video from the camera whose id contains the configured name, e.g.
   "Mire: Mire (synthetic moving picture)" for "Mire"; without a name, from
   the first camera of uvc.class, when there is one. */
static void choose_camera(void) {
    CORE_THREAD_ONLY();
    if (g.camera[0] == '\0' && g.vhi_cameras > 0) {
        copy_text(g.camera, sizeof(g.camera), "VHI:");
    }
    if (g.camera[0] == '\0') {
        return;
    }
    bctbx_list_t *cameras = linphone_core_get_video_devices_list(g.core);
    const char *found = NULL;
    for (const bctbx_list_t *it = cameras; it != NULL && found == NULL; it = bctbx_list_next(it)) {
        const char *id = bctbx_list_get_data(it);
        size_t len = strlen(g.camera);
        for (const char *p = id; *p != '\0' && found == NULL; ++p) {
            if (strncasecmp(p, g.camera, len) == 0) {
                found = id;
            }
        }
    }
    if (found == NULL || linphone_core_set_video_device(g.core, found) != 0) {
        char text[AOS_CORE_ADDR_MAX + 32];
        snprintf(text, sizeof(text), "no camera %s; video comes from %s", g.camera,
                 linphone_core_get_video_device(g.core) != NULL ? linphone_core_get_video_device(g.core) : "none");
        push_error(text);
    }
    bctbx_list_free_with_data(cameras, bctbx_free);
}

/* Video in both directions, shown by the AROS display filter on the surface
   of the interface, and offered and accepted in every call. */
static void setup_video(void) {
    CORE_THREAD_ONLY();
    linphone_core_set_video_display_filter(g.core, AOS_VIDEO_DISPLAY_NAME);
    void *window = g.config.video_window != NULL ? g.config.video_window : LINPHONE_VIDEO_DISPLAY_NONE;
    linphone_core_set_native_video_window_id(g.core, window);
    linphone_core_enable_video_capture(g.core, TRUE);
    linphone_core_enable_video_display(g.core, TRUE);
    LinphoneVideoActivationPolicy *policy = linphone_factory_create_video_activation_policy(g.factory);
    linphone_video_activation_policy_set_automatically_initiate(policy, TRUE);
    linphone_video_activation_policy_set_automatically_accept(policy, TRUE);
    linphone_core_set_video_activation_policy(g.core, policy);
    linphone_video_activation_policy_unref(policy);
    choose_camera();
}

static int create_core(void) {
    CORE_THREAD_ONLY();
    g.factory = linphone_factory_get();
    linphone_logging_service_set_log_level(linphone_logging_service_get(),
                                           g.config.verbose ? LinphoneLogLevelMessage : LinphoneLogLevelWarning);
    linphone_factory_set_top_resources_dir(g.factory, g.resources_dir);
    mkdir(g.data_dir, 0755);
    linphone_factory_set_data_dir(g.factory, g.data_dir);
    linphone_factory_set_config_dir(g.factory, g.data_dir);
    linphone_factory_set_download_dir(g.factory, g.data_dir);
    /* The default cache directory is the data directory with "cache/"
       appended without a separator, i.e. a sibling named <data>cache. */
    char cache_dir[PATH_MAX_LEN + 8];
    snprintf(cache_dir, sizeof(cache_dir), "%s/cache", g.data_dir);
    mkdir(cache_dir, 0755);
    linphone_factory_set_cache_dir(g.factory, cache_dir);
    if (g.record_dir[0] != '\0') {
        mkdir(g.record_dir, 0755); /* fails harmlessly when it exists */
    }

    char config_path[PATH_MAX_LEN + 16];
    snprintf(config_path, sizeof(config_path), "%s/linphonerc", g.data_dir);
    g.core = linphone_factory_create_core_3(g.factory, config_path, NULL, NULL);
    if (g.core == NULL) {
        return 0;
    }
    linphone_core_enable_ipv6(g.core, FALSE); /* AROSTCP cannot send to IPv4-mapped addresses */
    /* The other side picks the size and rate of its video from the
       bandwidth this side announces (b=AS). Without a limit a desktop client
       sends 1280x720 at 25 pictures per second, whose decoding takes the
       whole processor of a modest machine, sound included. linphonerc can
       set another limit ([net] download_bw); set before the core runs, this
       default is not saved there. */
    if (linphone_core_get_download_bandwidth(g.core) == 0) {
        linphone_core_set_download_bandwidth(g.core, DEFAULT_DOWNLOAD_KBIT);
    }
    linphone_core_set_user_agent(g.core, "aos-phone", "0.1");

    /* The devices of the program join the factory after the core has
       chosen its devices; see choose_ring_device() and choose_camera(). */
    MSFactory *ms_factory = linphone_core_get_ms_factory(g.core);
    aos_ahi_register(ms_factory, g.config.capture);
    aos_mire_register(ms_factory);
    g.vhi_cameras = aos_vhi_cameras_register(ms_factory);
    if (g.config.extra_camera != NULL && aos_camera_register(ms_factory, g.config.extra_camera) != 0) {
        push_error("the extra camera could not be added");
    }
    aos_video_display_register(ms_factory);
    linphone_core_reload_sound_devices(g.core);
    linphone_core_reload_video_devices(g.core);
    choose_ring_device();
    setup_video();
    if (g.config.capture_without_echo) {
        /* Before linphone_core_start(): the core saves the setting only
           once it runs. */
        linphone_core_enable_echo_cancellation(g.core, FALSE);
    }

    /* SIP over UDP only, on the configured port or a random one. */
    LinphoneTransports *transports = linphone_factory_create_transports(g.factory);
    linphone_transports_set_udp_port(transports, g.config.sip_port > 0 ? g.config.sip_port : LC_SIP_TRANSPORT_RANDOM);
    linphone_transports_set_tcp_port(transports, LC_SIP_TRANSPORT_DISABLED);
    linphone_transports_set_tls_port(transports, LC_SIP_TRANSPORT_DISABLED);
    linphone_core_set_transports(g.core, transports);
    linphone_transports_unref(transports);

    /* The linphone resolver would look for /etc/resolv.conf. */
    char servers[4][AOS_NET_ADDRSTRLEN];
    int count = aos_net_dns_servers(servers, 4);
    bctbx_list_t *dns = NULL;
    for (int i = 0; i < count && i < 4; ++i) {
        dns = bctbx_list_append(dns, servers[i]);
    }
    linphone_core_set_dns_servers(g.core, dns);
    bctbx_list_free(dns);

    /* ICE with a STUN server for every account without a policy of its own. */
    if (g.config.stun_server != NULL && g.config.stun_server[0] != '\0') {
        LinphoneNatPolicy *policy = linphone_core_create_nat_policy(g.core);
        linphone_nat_policy_set_stun_server(policy, g.config.stun_server);
        linphone_nat_policy_enable_stun(policy, TRUE);
        linphone_nat_policy_enable_ice(policy, TRUE);
        linphone_core_set_nat_policy(g.core, policy);
        linphone_nat_policy_unref(policy);
    }

    LinphoneCoreCbs *cbs = linphone_factory_create_core_cbs(g.factory);
    linphone_core_cbs_set_call_state_changed(cbs, on_call_state);
    linphone_core_cbs_set_account_registration_state_changed(cbs, on_account_state);
    linphone_core_cbs_set_message_received(cbs, on_message_received);
    linphone_core_add_callbacks(g.core, cbs);
    linphone_core_cbs_unref(cbs);

    if (linphone_core_start(g.core) != 0) {
        linphone_core_unref(g.core);
        g.core = NULL;
        return 0;
    }
    return 1;
}

static void run_command(struct command *cmd) {
    CORE_THREAD_ONLY();
    switch (cmd->type) {
    case CORE_CMD_REGISTER: do_register(cmd); break;
    case CORE_CMD_UNREGISTER: do_unregister(cmd); break;
    case CORE_CMD_REMOVE_ACCOUNT: do_remove_account(cmd); break;
    case CORE_CMD_SET_DEFAULT: do_set_default(cmd); break;
    case CORE_CMD_CALL: do_call(cmd); break;
    case CORE_CMD_ANSWER: do_answer(cmd->id); break;
    case CORE_CMD_HANGUP: do_hangup(cmd->id); break;
    case CORE_CMD_PLAY: do_play(cmd); break;
    case CORE_CMD_SNAPSHOT: do_snapshot(cmd); break;
    case CORE_CMD_VIDEO: do_set_video(cmd); break;
    case CORE_CMD_MESSAGE: do_message(cmd); break;
    case CORE_CMD_STOP: g.stop = 1; break;
    }
    memset(cmd->password, 0, sizeof(cmd->password));
}

static void destroy_core(void) {
    CORE_THREAD_ONLY();
    if (linphone_core_get_calls_nb(g.core) > 0) {
        linphone_core_terminate_all_calls(g.core);
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        while (linphone_core_get_calls_nb(g.core) > 0 && elapsed_ms(&start) < SHUTDOWN_MS) {
            linphone_core_iterate(g.core);
            usleep(CORE_TICK_US);
        }
    }
    for (int i = 0; i < MAX_MESSAGES; ++i) {
        if (g.messages[i].id != 0) {
            linphone_chat_message_unref(g.messages[i].message);
            memset(&g.messages[i], 0, sizeof(g.messages[i]));
        }
    }
    /* Removed accounts still unregistering lose their password now, so
       that linphonerc does not keep it. */
    for (int i = 0; i < MAX_LEAVING; ++i) {
        if (g.leaving[i][0] != '\0') {
            remove_password(g.leaving[i]);
            g.leaving[i][0] = '\0';
        }
    }
    linphone_core_stop(g.core);
    for (int i = 0; i < AOS_CORE_MAX_CALLS; ++i) {
        if (g.calls[i].id != 0) {
            free_call(&g.calls[i]);
        }
    }
    linphone_core_unref(g.core);
    g.core = NULL;
}

static void *core_main(void *arg) {
    (void)arg;
    g.self = pthread_self();
    CORE_THREAD_ONLY();
    if (!create_core()) {
        struct aos_core_event event;
        event_init(&event, AOS_EVENT_START_FAILED);
        push_event(&event);
    } else {
        push_started();
        struct timespec levels;
        clock_gettime(CLOCK_MONOTONIC, &levels);
        while (!g.stop) {
            struct command cmd;
            while (!g.stop && aos_queue_pop(&g.commands, &cmd)) {
                run_command(&cmd);
            }
            linphone_core_iterate(g.core);
            if (elapsed_ms(&levels) >= LEVELS_INTERVAL_MS) {
                report_levels();
                clock_gettime(CLOCK_MONOTONIC, &levels);
            }
            usleep(CORE_TICK_US);
        }
        destroy_core();
    }
    pthread_mutex_lock(&g.lock);
    g.finished = 1;
    pthread_mutex_unlock(&g.lock);
    return NULL;
}

/* ---- interface side ---- */

int aos_core_start(const struct aos_core_config *config) {
    if (g.started) {
        return -1;
    }
    g.config = *config;
    copy_text(g.data_dir, sizeof(g.data_dir), config->data_dir);
    copy_text(g.resources_dir, sizeof(g.resources_dir), config->resources_dir);
    copy_text(g.record_dir, sizeof(g.record_dir), config->record_dir);
    copy_text(g.camera, sizeof(g.camera), config->camera);
    g.finished = 0;
    g.stop = 0;
    if (aos_queue_init(&g.commands, g.command_storage, sizeof(struct command), COMMAND_CAPACITY) != 0) {
        return -1;
    }
    if (aos_queue_init(&g.events, g.event_storage, sizeof(struct aos_core_event), EVENT_CAPACITY) != 0) {
        aos_queue_destroy(&g.commands);
        return -1;
    }
    pthread_mutex_init(&g.lock, NULL);

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, CORE_STACK_SIZE);
    int rc = pthread_create(&g.thread, &attr, core_main, NULL);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        pthread_mutex_destroy(&g.lock);
        aos_queue_destroy(&g.events);
        aos_queue_destroy(&g.commands);
        return -1;
    }
    g.started = 1;
    return 0;
}

static int thread_finished(void) {
    pthread_mutex_lock(&g.lock);
    int finished = g.finished;
    pthread_mutex_unlock(&g.lock);
    return finished;
}

static int push_command(const struct command *cmd) {
    return g.started ? aos_queue_push(&g.commands, cmd) : -1;
}

void aos_core_stop(void) {
    if (!g.started) {
        return;
    }
    struct command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CORE_CMD_STOP;
    while (push_command(&cmd) != 0 && !thread_finished()) {
        usleep(CORE_TICK_US);
    }
    pthread_join(g.thread, NULL);
    g.started = 0;
    pthread_mutex_destroy(&g.lock);
    aos_queue_destroy(&g.events);
    aos_queue_destroy(&g.commands);
}

int aos_core_next_event(struct aos_core_event *event) {
    return g.started ? aos_queue_pop(&g.events, event) : 0;
}

int aos_core_register(const char *identity, const char *password, const char *server) {
    struct command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CORE_CMD_REGISTER;
    copy_text(cmd.address, sizeof(cmd.address), identity);
    copy_text(cmd.password, sizeof(cmd.password), password);
    copy_text(cmd.server, sizeof(cmd.server), server);
    int rc = push_command(&cmd);
    memset(cmd.password, 0, sizeof(cmd.password));
    return rc;
}

static int simple_command(enum command_type type, int id, const char *address) {
    struct command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = type;
    cmd.id = id;
    copy_text(cmd.address, sizeof(cmd.address), address);
    return push_command(&cmd);
}

int aos_core_unregister(const char *identity) {
    return simple_command(CORE_CMD_UNREGISTER, 0, identity);
}

int aos_core_remove_account(const char *identity) {
    return simple_command(CORE_CMD_REMOVE_ACCOUNT, 0, identity);
}

int aos_core_set_default_account(const char *identity) {
    return simple_command(CORE_CMD_SET_DEFAULT, 0, identity);
}

int aos_core_call(const char *address) {
    return simple_command(CORE_CMD_CALL, 0, address);
}

int aos_core_answer(int call_id) {
    return simple_command(CORE_CMD_ANSWER, call_id, NULL);
}

int aos_core_hangup(int call_id) {
    return simple_command(CORE_CMD_HANGUP, call_id, NULL);
}

int aos_core_play(int call_id, const char *file) {
    struct command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CORE_CMD_PLAY;
    cmd.id = call_id;
    copy_text(cmd.text, sizeof(cmd.text), file);
    return push_command(&cmd);
}

int aos_core_snapshot(int call_id, const char *file) {
    struct command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CORE_CMD_SNAPSHOT;
    cmd.id = call_id;
    copy_text(cmd.text, sizeof(cmd.text), file);
    return push_command(&cmd);
}

int aos_core_set_video(int call_id, int on) {
    struct command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CORE_CMD_VIDEO;
    cmd.id = call_id;
    cmd.flag = on != 0;
    return push_command(&cmd);
}

int aos_core_send_message(const char *peer, const char *text) {
    struct command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CORE_CMD_MESSAGE;
    cmd.id = ++g.next_message_id;
    copy_text(cmd.address, sizeof(cmd.address), peer);
    copy_utf8(cmd.text, sizeof(cmd.text), text);
    return push_command(&cmd) == 0 ? cmd.id : -1;
}
