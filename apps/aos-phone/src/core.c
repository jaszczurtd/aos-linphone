/*
 * The linphone core thread. This is the only file of aos-phone that uses
 * liblinphone; every function that calls it starts with CORE_THREAD_ONLY(),
 * which stops the program when it runs in any other thread.
 */
#include "core.h"
#include "queue.h"

#include "aos/net.h"

#include <linphone/core.h>
#include <linphone/factory.h>

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define CORE_STACK_SIZE    (1024 * 1024)
#define CORE_TICK_US       20000
#define LEVELS_INTERVAL_MS 250
#define EVENT_WAIT_MS      2000 /* longest wait for room in a full event queue */
#define SHUTDOWN_MS        2000 /* time given to calls to end before the core stops */
#define EVENT_CAPACITY     64
#define COMMAND_CAPACITY   16
#define MAX_MESSAGES       16 /* sent messages waiting for their final state */
#define PASSWORD_MAX       128
#define PATH_MAX_LEN       256

enum command_type {
    CMD_REGISTER,
    CMD_UNREGISTER,
    CMD_CALL,
    CMD_ANSWER,
    CMD_HANGUP,
    CMD_MESSAGE,
    CMD_STOP
};

struct command {
    enum command_type type;
    int id;
    char address[AOS_CORE_ADDR_MAX]; /* identity, callee or message peer */
    char server[AOS_CORE_ADDR_MAX];
    char password[PASSWORD_MAX];
    char text[AOS_CORE_TEXT_MAX];
};

struct call_slot {
    int id; /* 0: free */
    int state;
    LinphoneCall *call;
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

    /* Core thread only. */
    pthread_t self;
    LinphoneFactory *factory;
    LinphoneCore *core;
    struct call_slot calls[AOS_CORE_MAX_CALLS];
    struct message_slot messages[MAX_MESSAGES];
    int next_call_id;
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
    if (reported >= 0 && reported != slot->state) {
        /* End and Error are final for the interface; Released follows. */
        slot->state = reported;
        struct aos_core_event event;
        event_init(&event, AOS_EVENT_CALL);
        event.id = slot->id;
        event.state = reported;
        copy_address(event.peer, sizeof(event.peer), linphone_call_get_remote_address(call));
        copy_text(event.text, sizeof(event.text), message);
        push_event(&event);
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
    /* Refreshing renews a registration that is still valid. */
    case LinphoneRegistrationOk:
    case LinphoneRegistrationRefreshing:
        return AOS_REG_OK;
    case LinphoneRegistrationCleared: return AOS_REG_CLEARED;
    case LinphoneRegistrationFailed: return AOS_REG_FAILED;
    default: return AOS_REG_NONE;
    }
}

static void on_account_state(LinphoneCore *core, LinphoneAccount *account, LinphoneRegistrationState state,
                             const char *message) {
    (void)core;
    CORE_THREAD_ONLY();
    struct aos_core_event event;
    event_init(&event, AOS_EVENT_ACCOUNT);
    event.state = map_reg_state(state);
    copy_address(event.peer, sizeof(event.peer),
                 linphone_account_params_get_identity_address(linphone_account_get_params(account)));
    copy_text(event.text, sizeof(event.text), message);
    push_event(&event);
}

static void do_register(const struct command *cmd) {
    CORE_THREAD_ONLY();
    LinphoneAddress *identity = linphone_factory_create_address(g.factory, cmd->address);
    if (identity == NULL || linphone_address_get_username(identity) == NULL) {
        push_error("invalid identity");
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
        push_error("invalid server address");
        linphone_address_unref(identity);
        return;
    }

    linphone_core_clear_accounts(g.core);
    linphone_core_clear_all_auth_info(g.core);

    LinphoneAuthInfo *auth = linphone_factory_create_auth_info_2(
        g.factory, linphone_address_get_username(identity), NULL, cmd->password, NULL, NULL,
        linphone_address_get_domain(identity), NULL);
    linphone_core_add_auth_info(g.core, auth);
    linphone_auth_info_unref(auth);

    LinphoneAccountParams *params = linphone_core_create_account_params(g.core);
    linphone_account_params_set_identity_address(params, identity);
    linphone_account_params_set_server_address(params, server);
    linphone_account_params_enable_register(params, TRUE);
    LinphoneAccount *account = linphone_core_create_account(g.core, params);
    linphone_account_params_unref(params);
    linphone_core_add_account(g.core, account);
    linphone_core_set_default_account(g.core, account);
    linphone_account_unref(account);
    linphone_address_unref(identity);
    linphone_address_unref(server);
}

static void do_unregister(void) {
    CORE_THREAD_ONLY();
    LinphoneAccount *account = linphone_core_get_default_account(g.core);
    if (account == NULL) {
        push_error("no account to unregister");
        return;
    }
    LinphoneAccountParams *params = linphone_account_params_clone(linphone_account_get_params(account));
    linphone_account_params_enable_register(params, FALSE);
    linphone_account_set_params(account, params);
    linphone_account_params_unref(params);
}

/* ---- call requests ---- */

static void do_call(const struct command *cmd) {
    CORE_THREAD_ONLY();
    LinphoneAddress *address = linphone_core_interpret_url_2(g.core, cmd->address, FALSE);
    if (address == NULL) {
        push_error("invalid address to call");
        return;
    }
    if (linphone_core_invite_address(g.core, address) == NULL) {
        push_error("the call could not be started");
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
    if (linphone_call_accept(slot->call) != 0) {
        push_error("the call could not be answered");
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
}

static int create_core(void) {
    CORE_THREAD_ONLY();
    g.factory = linphone_factory_get();
    linphone_logging_service_set_log_level(linphone_logging_service_get(), LinphoneLogLevelWarning);
    linphone_factory_set_top_resources_dir(g.factory, g.resources_dir);
    mkdir(g.data_dir, 0755);
    linphone_factory_set_data_dir(g.factory, g.data_dir);
    linphone_factory_set_config_dir(g.factory, g.data_dir);
    linphone_factory_set_download_dir(g.factory, g.data_dir);

    char config_path[PATH_MAX_LEN + 16];
    snprintf(config_path, sizeof(config_path), "%s/linphonerc", g.data_dir);
    g.core = linphone_factory_create_core_3(g.factory, config_path, NULL, NULL);
    if (g.core == NULL) {
        return 0;
    }
    linphone_core_enable_ipv6(g.core, FALSE); /* AROSTCP cannot send to IPv4-mapped addresses */
    linphone_core_set_user_agent(g.core, "aos-phone", "0.1");

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
    case CMD_REGISTER: do_register(cmd); break;
    case CMD_UNREGISTER: do_unregister(); break;
    case CMD_CALL: do_call(cmd); break;
    case CMD_ANSWER: do_answer(cmd->id); break;
    case CMD_HANGUP: do_hangup(cmd->id); break;
    case CMD_MESSAGE: do_message(cmd); break;
    case CMD_STOP: g.stop = 1; break;
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
    cmd.type = CMD_STOP;
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
    cmd.type = CMD_REGISTER;
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

int aos_core_unregister(void) {
    return simple_command(CMD_UNREGISTER, 0, NULL);
}

int aos_core_call(const char *address) {
    return simple_command(CMD_CALL, 0, address);
}

int aos_core_answer(int call_id) {
    return simple_command(CMD_ANSWER, call_id, NULL);
}

int aos_core_hangup(int call_id) {
    return simple_command(CMD_HANGUP, call_id, NULL);
}

int aos_core_send_message(const char *peer, const char *text) {
    struct command cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.type = CMD_MESSAGE;
    cmd.id = ++g.next_message_id;
    copy_text(cmd.address, sizeof(cmd.address), peer);
    copy_utf8(cmd.text, sizeof(cmd.text), text);
    return push_command(&cmd) == 0 ? cmd.id : -1;
}
