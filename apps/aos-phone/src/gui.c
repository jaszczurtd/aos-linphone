/*
 * The Zune interface. The application object is a subclass of Application
 * whose methods carry out the button actions, the ARexx commands and the
 * handling of core events. The core thread wakes the interface with an Exec
 * signal registered as an input handler, so events are handled inside any
 * Zune input loop.
 *
 * Text from the core is UTF-8 and goes through text.c before Zune shows it;
 * what the user types is in the system code page and becomes UTF-8 before
 * it reaches the core.
 */
#include "gui.h"
#include "core.h"
#include "text.h"

#include <exec/types.h>
#include <libraries/mui.h>
#include <rexx/errors.h>
#include <utility/hooks.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_CHATS      8
#define CHAT_LINES_MAX 200
#define INPUT_MAX      512 /* bytes a user types into a line, in the code page */
#define LINE_MAX_LEN   (AOS_CORE_TEXT_MAX + AOS_CORE_ADDR_MAX + 32)
#define STATUS_MAX     (AOS_CORE_TEXT_MAX + 64)

#define PHONE_METHOD(n)       (TAG_USER | 0x3a5a0000 | (n))
#define MUIM_Phone_CoreEvents PHONE_METHOD(1)
#define MUIM_Phone_Tick       PHONE_METHOD(2)
#define MUIM_Phone_Register   PHONE_METHOD(3)
#define MUIM_Phone_Unregister PHONE_METHOD(4)
#define MUIM_Phone_Call       PHONE_METHOD(5)
#define MUIM_Phone_Answer     PHONE_METHOD(6)
#define MUIM_Phone_Hangup     PHONE_METHOD(7)
#define MUIM_Phone_OpenChat   PHONE_METHOD(8)
#define MUIM_Phone_SendChat   PHONE_METHOD(9)
#define MUIM_Phone_ShowCall   PHONE_METHOD(10)
#define MUIM_Phone_Rexx       PHONE_METHOD(11)

struct phone_msg {
    STACKED ULONG MethodID;
    STACKED IPTR arg1;
    STACKED IPTR arg2;
};

enum rexx_command {
    RX_REGISTER,
    RX_UNREGISTER,
    RX_CALL,
    RX_ANSWER,
    RX_HANGUP,
    RX_SEND,
    RX_STATUS,
    RX_QUIT,
    RX_COUNT
};

struct gui_call {
    int id; /* 0: free */
    int state;
    char peer[AOS_CORE_ADDR_MAX]; /* UTF-8 */
    char reason[AOS_CORE_ADDR_MAX];
    time_t connected_at;
};

struct chat {
    Object *win;
    Object *list;
    Object *input;
    int open_lines;
    unsigned last_used;
    char peer[AOS_CORE_ADDR_MAX]; /* UTF-8, as typed or as received */
    char title[AOS_CORE_ADDR_MAX + 16];
};

static struct {
    struct MUI_CustomClass *phone_class;
    Object *app;
    struct Task *task;
    BYTE signal;
    struct MUI_InputHandlerNode events_ihn;
    struct MUI_InputHandlerNode timer_ihn;
    int handlers_added;

    Object *main_win, *account_win, *call_win;
    Object *account_text, *address_str, *calls_view, *calls_list, *status_text;
    Object *answer_button, *hangup_button;
    Object *identity_str, *password_str, *server_str;
    Object *call_peer_text, *call_state_text, *call_time_text, *mic_gauge, *speaker_gauge;
    Object *call_answer_button, *call_hangup_button;

    struct gui_call calls[AOS_CORE_MAX_CALLS];
    int rows[AOS_CORE_MAX_CALLS]; /* call id of each row of the call list */
    struct gui_call shown;        /* the call in the call window; id 0: none */
    struct chat chats[MAX_CHATS];
    unsigned use_counter;
    int reg_state;
    int sip_port;
    char identity[AOS_CORE_ADDR_MAX]; /* UTF-8 */
    struct Hook rexx_hooks[RX_COUNT];
} gui;

/* Zune compares the command word over its own length, so no name may
   start with an earlier, shorter one. */
static struct MUI_Command rexx_commands[] = {
    {"REGISTER", "IDENTITY/A,PASSWORD/A,SERVER", 3, &gui.rexx_hooks[RX_REGISTER], {0}},
    {"UNREGISTER", NULL, 0, &gui.rexx_hooks[RX_UNREGISTER], {0}},
    {"CALL", "ADDRESS/A", 1, &gui.rexx_hooks[RX_CALL], {0}},
    {"ANSWER", NULL, 0, &gui.rexx_hooks[RX_ANSWER], {0}},
    {"HANGUP", NULL, 0, &gui.rexx_hooks[RX_HANGUP], {0}},
    {"SEND", "TO/A,TEXT/A/F", 2, &gui.rexx_hooks[RX_SEND], {0}},
    {"STATUS", NULL, 0, &gui.rexx_hooks[RX_STATUS], {0}},
    {"QUIT", NULL, 0, &gui.rexx_hooks[RX_QUIT], {0}},
    {NULL, NULL, 0, NULL, {0}},
};

/* ---- text helpers ---- */

/* Zune text and lists read ESC sequences as formatting; received text must
   not set any. */
static void strip_controls(char *text) {
    for (; *text != '\0'; ++text) {
        if ((unsigned char)*text < 0x20) {
            *text = ' ';
        }
    }
}

static void to_display(const char *utf8, char *out, size_t size) {
    aos_text_from_utf8(utf8, out, size);
    strip_controls(out);
}

/* Copies a string, cut to the size of dst. */
static void copy_text(char *dst, size_t size, const char *src) {
    size_t len = strnlen(src, size - 1);
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Returns the contents of a string gadget converted to UTF-8. */
static void string_utf8(Object *obj, char *out, size_t size) {
    STRPTR contents = NULL;
    get(obj, MUIA_String_Contents, &contents);
    aos_text_to_utf8(contents != NULL ? (const char *)contents : "", out, size);
}

static void set_text(Object *obj, const char *text) {
    set(obj, MUIA_Text_Contents, (IPTR)text);
}

static void status(const char *format, ...) {
    char text[STATUS_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    printf("aos-phone: %s\n", text);
    char shown[STATUS_MAX];
    to_display(text, shown, sizeof(shown));
    set_text(gui.status_text, shown);
}

static const char *reg_name(int state) {
    switch (state) {
    case AOS_REG_PROGRESS: return "registering";
    case AOS_REG_OK: return "registered";
    case AOS_REG_CLEARED: return "unregistered";
    case AOS_REG_FAILED: return "registration failed";
    default: return "not registered";
    }
}

static const char *call_state_name(int state) {
    switch (state) {
    case AOS_CALL_INCOMING: return "incoming";
    case AOS_CALL_OUTGOING: return "calling";
    case AOS_CALL_RINGING: return "ringing";
    case AOS_CALL_CONNECTED: return "connected";
    case AOS_CALL_PAUSED: return "on hold";
    case AOS_CALL_ENDED: return "ended";
    case AOS_CALL_FAILED: return "failed";
    default: return "?";
    }
}

static const char *msg_state_name(int state) {
    switch (state) {
    case AOS_MSG_SENDING: return "sending";
    case AOS_MSG_DELIVERED: return "delivered";
    default: return "not delivered";
    }
}

static int call_final(int state) {
    return state == AOS_CALL_ENDED || state == AOS_CALL_FAILED;
}

/* The part of a SIP address that names the person: the user of
   sip:user@host, or the whole text when it has no user part. */
static void peer_key(const char *peer, char *key, size_t size) {
    if (strncmp(peer, "sip:", 4) == 0) {
        peer += 4;
    } else if (strncmp(peer, "sips:", 5) == 0) {
        peer += 5;
    }
    size_t len = strcspn(peer, "@;>");
    if (len >= size) {
        len = size - 1;
    }
    memcpy(key, peer, len);
    key[len] = '\0';
}

/* ---- account ---- */

static void show_account(void) {
    char line[AOS_CORE_ADDR_MAX + 64], shown[sizeof(line)];
    if (gui.identity[0] != '\0') {
        snprintf(line, sizeof(line), "%s: %s", gui.identity, reg_name(gui.reg_state));
    } else {
        snprintf(line, sizeof(line), "no account");
    }
    to_display(line, shown, sizeof(shown));
    set_text(gui.account_text, shown);
}

static IPTR do_register(void) {
    char identity[AOS_CORE_ADDR_MAX], password[128], server[AOS_CORE_ADDR_MAX];
    string_utf8(gui.identity_str, identity, sizeof(identity));
    string_utf8(gui.password_str, password, sizeof(password));
    string_utf8(gui.server_str, server, sizeof(server));
    if (identity[0] == '\0') {
        status("enter the identity, e.g. sip:alice@example.org");
        return FALSE;
    }
    int rc = aos_core_register(identity, password, server);
    memset(password, 0, sizeof(password));
    if (rc != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    copy_text(gui.identity, sizeof(gui.identity), identity);
    status("registering %s", identity);
    return TRUE;
}

static IPTR do_unregister(void) {
    if (aos_core_unregister() != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    return TRUE;
}

/* ---- calls ---- */

static struct gui_call *find_call(int id) {
    for (int i = 0; i < AOS_CORE_MAX_CALLS; ++i) {
        if (gui.calls[i].id == id && id != 0) {
            return &gui.calls[i];
        }
    }
    return NULL;
}

static void format_duration(const struct gui_call *call, char *out, size_t size) {
    if (call->connected_at == 0 || call_final(call->state)) {
        out[0] = '\0';
        return;
    }
    long seconds = (long)(time(NULL) - call->connected_at);
    snprintf(out, size, "%02ld:%02ld", seconds / 60, seconds % 60);
}

static void show_call_window(void) {
    const struct gui_call *call = &gui.shown;
    char line[LINE_MAX_LEN], shown[LINE_MAX_LEN];
    if (call->id == 0) {
        set_text(gui.call_peer_text, "no call");
        set_text(gui.call_state_text, "");
        set_text(gui.call_time_text, "");
    } else {
        to_display(call->peer, shown, sizeof(shown));
        set_text(gui.call_peer_text, shown);
        if (call_final(call->state) && call->reason[0] != '\0') {
            snprintf(line, sizeof(line), "%s: %s", call_state_name(call->state), call->reason);
        } else {
            snprintf(line, sizeof(line), "%s", call_state_name(call->state));
        }
        to_display(line, shown, sizeof(shown));
        set_text(gui.call_state_text, shown);
        format_duration(call, line, sizeof(line));
        set_text(gui.call_time_text, line);
    }
    if (call->id == 0 || call->state != AOS_CALL_CONNECTED) {
        set(gui.mic_gauge, MUIA_Gauge_Current, 0);
        set(gui.speaker_gauge, MUIA_Gauge_Current, 0);
    }
    BOOL can_answer = call->id != 0 && call->state == AOS_CALL_INCOMING;
    BOOL can_hangup = call->id != 0 && !call_final(call->state);
    set(gui.call_answer_button, MUIA_Disabled, !can_answer);
    set(gui.answer_button, MUIA_Disabled, !can_answer);
    set(gui.call_hangup_button, MUIA_Disabled, !can_hangup);
    set(gui.hangup_button, MUIA_Disabled, !can_hangup);
}

static void show_call_list(void) {
    char line[LINE_MAX_LEN], shown[LINE_MAX_LEN];
    int row = 0, active = MUIV_List_Active_Off;
    DoMethod(gui.calls_list, MUIM_List_Clear);
    for (int i = 0; i < AOS_CORE_MAX_CALLS; ++i) {
        const struct gui_call *call = &gui.calls[i];
        if (call->id == 0) {
            continue;
        }
        snprintf(line, sizeof(line), "%s - %s", call->peer, call_state_name(call->state));
        to_display(line, shown, sizeof(shown));
        DoMethod(gui.calls_list, MUIM_List_InsertSingle, (IPTR)shown, MUIV_List_Insert_Bottom);
        if (call->id == gui.shown.id) {
            active = row;
        }
        gui.rows[row++] = call->id;
    }
    nnset(gui.calls_list, MUIA_List_Active, active);
}

static void on_call_event(const struct aos_core_event *event) {
    struct gui_call *call = find_call(event->id);
    if (call == NULL) {
        if (call_final(event->state)) {
            call = NULL;
        } else {
            for (int i = 0; i < AOS_CORE_MAX_CALLS && call == NULL; ++i) {
                if (gui.calls[i].id == 0) {
                    call = &gui.calls[i];
                    memset(call, 0, sizeof(*call));
                    call->id = event->id;
                }
            }
        }
    }
    struct gui_call update;
    memset(&update, 0, sizeof(update));
    if (call != NULL) {
        update = *call;
    }
    update.id = event->id;
    update.state = event->state;
    copy_text(update.peer, sizeof(update.peer), event->peer);
    copy_text(update.reason, sizeof(update.reason), event->text);
    if (event->state == AOS_CALL_CONNECTED && update.connected_at == 0) {
        update.connected_at = time(NULL);
    }
    if (call != NULL) {
        *call = update;
        if (call_final(event->state)) {
            memset(call, 0, sizeof(*call));
        }
    }

    /* The call window follows a new call unless it shows one in progress. */
    BOOL is_new = event->state == AOS_CALL_INCOMING || event->state == AOS_CALL_OUTGOING;
    if (update.id == gui.shown.id || (is_new && (gui.shown.id == 0 || call_final(gui.shown.state)))) {
        gui.shown = update;
        if (is_new) {
            set(gui.call_win, MUIA_Window_Open, TRUE);
        }
    }
    show_call_list();
    show_call_window();
}

static IPTR do_call(void) {
    char address[AOS_CORE_ADDR_MAX];
    string_utf8(gui.address_str, address, sizeof(address));
    if (address[0] == '\0') {
        status("enter an address or a user name to call");
        return FALSE;
    }
    if (aos_core_call(address) != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    status("calling %s", address);
    return TRUE;
}

/* The buttons and the ARexx commands act on the call in the call window,
   or let the core pick: the first incoming call, the current call. */
static IPTR do_answer(void) {
    int id = gui.shown.id != 0 && gui.shown.state == AOS_CALL_INCOMING ? gui.shown.id : 0;
    if (aos_core_answer(id) != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    return TRUE;
}

static IPTR do_hangup(void) {
    int id = gui.shown.id != 0 && !call_final(gui.shown.state) ? gui.shown.id : 0;
    if (aos_core_hangup(id) != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    return TRUE;
}

static IPTR do_show_call(void) {
    IPTR row = MUIV_List_Active_Off;
    get(gui.calls_list, MUIA_List_Active, &row);
    if ((LONG)row >= 0 && (LONG)row < AOS_CORE_MAX_CALLS) {
        struct gui_call *call = find_call(gui.rows[row]);
        if (call != NULL) {
            gui.shown = *call;
            show_call_window();
        }
    }
    return 0;
}

static IPTR do_tick(void) {
    if (gui.shown.id != 0 && gui.shown.state == AOS_CALL_CONNECTED) {
        char text[16];
        format_duration(&gui.shown, text, sizeof(text));
        set_text(gui.call_time_text, text);
    }
    return 0;
}

static int level_percent(float db) {
    /* Show the range from -60 dBm0 to full scale. */
    float percent = (db + 60.0f) * 100.0f / 60.0f;
    return percent < 0.0f ? 0 : percent > 100.0f ? 100
                                                 : (int)percent;
}

/* ---- chat ---- */

static struct chat *find_chat(const char *peer) {
    char key[AOS_CORE_ADDR_MAX], other[AOS_CORE_ADDR_MAX];
    peer_key(peer, key, sizeof(key));
    for (int i = 0; i < MAX_CHATS; ++i) {
        if (gui.chats[i].win != NULL) {
            peer_key(gui.chats[i].peer, other, sizeof(other));
            if (strcmp(key, other) == 0) {
                return &gui.chats[i];
            }
        }
    }
    return NULL;
}

static Object *create_chat_window(int index) {
    struct chat *chat = &gui.chats[index];
    Object *send = NULL;
    /* clang-format off */
    Object *win = WindowObject,
        MUIA_Window_Title, (IPTR)chat->title,
        MUIA_Window_ID, MAKE_ID('A', 'P', 'C', '0' + index),
        MUIA_Window_Width, MUIV_Window_Width_Screen(40),
        MUIA_Window_Height, MUIV_Window_Height_Screen(40),
        WindowContents, VGroup,
            Child, ListviewObject,
                MUIA_Listview_Input, FALSE,
                MUIA_Listview_List, chat->list = ListObject,
                    ReadListFrame,
                    MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                    MUIA_List_DestructHook, MUIV_List_DestructHook_String,
                End,
            End,
            Child, HGroup,
                Child, chat->input = StringObject,
                    StringFrame,
                    MUIA_String_MaxLen, INPUT_MAX,
                    MUIA_CycleChain, 1,
                End,
                Child, send = SimpleButton("_Send"),
            End,
        End,
    End;
    /* clang-format on */
    if (win == NULL) {
        return NULL;
    }
    set(send, MUIA_CycleChain, 1);
    DoMethod(gui.app, OM_ADDMEMBER, (IPTR)win);
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, MUIV_Notify_Self, 3, MUIM_Set, MUIA_Window_Open, FALSE);
    DoMethod(send, MUIM_Notify, MUIA_Pressed, FALSE, gui.app, 2, MUIM_Phone_SendChat, index);
    DoMethod(chat->input, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, gui.app, 2, MUIM_Phone_SendChat, index);
    return win;
}

/* Sets the address the window sends to, and its title. */
static void set_chat_peer(struct chat *chat, const char *peer) {
    char shown[AOS_CORE_ADDR_MAX];
    copy_text(chat->peer, sizeof(chat->peer), peer);
    to_display(peer, shown, sizeof(shown));
    snprintf(chat->title, sizeof(chat->title), "Chat: %s", shown);
    if (chat->win != NULL) {
        set(chat->win, MUIA_Window_Title, (IPTR)chat->title);
    }
}

/* Returns the chat window of the peer, opened. Windows match by the user
   part of the address; a new peer takes a free window, or the one used least
   recently. typed is TRUE for an address the user gave: the window then
   sends to it, while a received message keeps the address the window has. */
static struct chat *open_chat(const char *peer, BOOL typed) {
    struct chat *chat = find_chat(peer);
    if (chat == NULL) {
        int pick = -1;
        for (int i = 0; i < MAX_CHATS && pick < 0; ++i) {
            if (gui.chats[i].win == NULL) {
                pick = i;
            }
        }
        if (pick < 0) {
            pick = 0;
            for (int i = 1; i < MAX_CHATS; ++i) {
                if (gui.chats[i].last_used < gui.chats[pick].last_used) {
                    pick = i;
                }
            }
        }
        chat = &gui.chats[pick];
        if (chat->win != NULL) {
            DoMethod(chat->list, MUIM_List_Clear);
        }
        chat->open_lines = 0;
        set_chat_peer(chat, peer);
        if (chat->win == NULL) {
            chat->win = create_chat_window(pick);
            if (chat->win == NULL) {
                status("cannot open a chat window");
                return NULL;
            }
        }
    } else if (typed && strcmp(chat->peer, peer) != 0) {
        set_chat_peer(chat, peer);
    }
    chat->last_used = ++gui.use_counter;
    set(chat->win, MUIA_Window_Open, TRUE);
    return chat;
}

static void chat_line(struct chat *chat, const char *who, const char *utf8) {
    char line[LINE_MAX_LEN], shown[LINE_MAX_LEN];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    snprintf(line, sizeof(line), "[%02d:%02d] %s: %s", tm != NULL ? tm->tm_hour : 0, tm != NULL ? tm->tm_min : 0,
             who, utf8);
    to_display(line, shown, sizeof(shown));
    DoMethod(chat->list, MUIM_List_InsertSingle, (IPTR)shown, MUIV_List_Insert_Bottom);
    if (++chat->open_lines > CHAT_LINES_MAX) {
        DoMethod(chat->list, MUIM_List_Remove, MUIV_List_Remove_First);
        --chat->open_lines;
    }
    DoMethod(chat->list, MUIM_List_Jump, MUIV_List_Jump_Bottom);
}

static int send_text(struct chat *chat, const char *utf8) {
    int id = aos_core_send_message(chat->peer, utf8);
    if (id < 0) {
        chat_line(chat, "!", "the core is busy; the message was not sent");
        return -1;
    }
    chat_line(chat, "me", utf8);
    printf("aos-phone: message %d to %s: %s\n", id, chat->peer, utf8);
    return 0;
}

static IPTR do_send_chat(int index) {
    if (index < 0 || index >= MAX_CHATS || gui.chats[index].win == NULL) {
        return FALSE;
    }
    struct chat *chat = &gui.chats[index];
    char text[AOS_CORE_TEXT_MAX];
    string_utf8(chat->input, text, sizeof(text));
    if (text[0] == '\0') {
        return FALSE;
    }
    int rc = send_text(chat, text);
    set(chat->input, MUIA_String_Contents, (IPTR) "");
    set(chat->win, MUIA_Window_ActiveObject, (IPTR)chat->input);
    chat->last_used = ++gui.use_counter;
    return rc == 0;
}

static IPTR do_open_chat(void) {
    char address[AOS_CORE_ADDR_MAX];
    string_utf8(gui.address_str, address, sizeof(address));
    if (address[0] == '\0') {
        status("enter an address or a user name to chat with");
        return FALSE;
    }
    struct chat *chat = open_chat(address, TRUE);
    if (chat != NULL) {
        set(chat->win, MUIA_Window_ActiveObject, (IPTR)chat->input);
    }
    return chat != NULL;
}

/* ---- core events ---- */

static void on_event(const struct aos_core_event *event) {
    char who[AOS_CORE_ADDR_MAX];
    struct chat *chat;
    switch (event->type) {
    case AOS_EVENT_STARTED:
        gui.sip_port = event->id;
        printf("aos-phone: core started; SIP UDP port %d, account %s, server %s\n", event->id,
               event->peer[0] ? event->peer : "none", event->text);
        if (event->peer[0] != '\0') {
            char shown[AOS_CORE_ADDR_MAX];
            copy_text(gui.identity, sizeof(gui.identity), event->peer);
            to_display(event->peer, shown, sizeof(shown));
            set(gui.identity_str, MUIA_String_Contents, (IPTR)shown);
            to_display(event->text, shown, sizeof(shown));
            set(gui.server_str, MUIA_String_Contents, (IPTR)shown);
        }
        show_account();
        break;
    case AOS_EVENT_START_FAILED:
        status("the linphone core could not start; check the network and PROGDIR:share");
        break;
    case AOS_EVENT_ACCOUNT:
        gui.reg_state = event->state;
        if (event->peer[0] != '\0') {
            copy_text(gui.identity, sizeof(gui.identity), event->peer);
        }
        show_account();
        status("account %s: %s%s%s", event->peer, reg_name(event->state), event->text[0] ? " - " : "", event->text);
        break;
    case AOS_EVENT_CALL:
        printf("aos-phone: call %d %s: %s (%s)\n", event->id, call_state_name(event->state), event->peer, event->text);
        on_call_event(event);
        if (event->state == AOS_CALL_INCOMING) {
            status("incoming call from %s", event->peer);
        } else if (event->state == AOS_CALL_CONNECTED) {
            status("connected with %s", event->peer);
        } else if (call_final(event->state)) {
            status("call with %s %s%s%s", event->peer, call_state_name(event->state), event->text[0] ? ": " : "",
                   event->text);
        }
        break;
    case AOS_EVENT_LEVELS:
        if (event->id == gui.shown.id) {
            set(gui.speaker_gauge, MUIA_Gauge_Current, level_percent(event->play_db));
            set(gui.mic_gauge, MUIA_Gauge_Current, level_percent(event->record_db));
        }
        break;
    case AOS_EVENT_MESSAGE:
        printf("aos-phone: message from %s: %s\n", event->peer, event->text);
        chat = open_chat(event->peer, FALSE);
        if (chat != NULL) {
            peer_key(event->peer, who, sizeof(who));
            chat_line(chat, who, event->text);
        }
        break;
    case AOS_EVENT_MESSAGE_STATE:
        printf("aos-phone: message %d to %s: %s\n", event->id, event->peer, msg_state_name(event->state));
        if (event->state == AOS_MSG_FAILED && (chat = find_chat(event->peer)) != NULL) {
            chat_line(chat, "!", "the last message was not delivered");
        }
        break;
    case AOS_EVENT_ERROR:
        status("%s", event->text);
        break;
    case AOS_EVENT_LOST:
        status("%d core events were lost while the interface was busy", event->id);
        break;
    }
}

static IPTR do_core_events(void) {
    struct aos_core_event event;
    while (aos_core_next_event(&event)) {
        on_event(&event);
    }
    return 0;
}

/* ---- ARexx ---- */

static void rexx_result(const char *text) {
    set(gui.app, MUIA_Application_RexxString, (IPTR)text);
}

static IPTR do_rexx(int command, IPTR *args) {
    char a[AOS_CORE_ADDR_MAX], b[AOS_CORE_TEXT_MAX], c[AOS_CORE_ADDR_MAX];
    char result[STATUS_MAX];
    IPTR ok = TRUE;
    snprintf(result, sizeof(result), "OK");
    switch (command) {
    case RX_REGISTER:
        /* The same path as the Register button, through the account window. */
        set(gui.identity_str, MUIA_String_Contents, args[0]);
        set(gui.password_str, MUIA_String_Contents, args[1]);
        set(gui.server_str, MUIA_String_Contents, args[2] != 0 ? args[2] : (IPTR) "");
        ok = do_register();
        set(gui.password_str, MUIA_String_Contents, (IPTR) "");
        break;
    case RX_UNREGISTER: ok = do_unregister(); break;
    case RX_CALL:
        set(gui.address_str, MUIA_String_Contents, args[0]);
        ok = do_call();
        break;
    case RX_ANSWER: ok = do_answer(); break;
    case RX_HANGUP: ok = do_hangup(); break;
    case RX_SEND: {
        aos_text_to_utf8((const char *)args[0], a, sizeof(a));
        aos_text_to_utf8((const char *)args[1], b, sizeof(b));
        struct chat *chat = open_chat(a, TRUE);
        ok = chat != NULL && send_text(chat, b) == 0;
        break;
    }
    case RX_STATUS: {
        int n = snprintf(result, sizeof(result), "%s|port %d", reg_name(gui.reg_state), gui.sip_port);
        for (int i = 0; i < AOS_CORE_MAX_CALLS && n > 0 && (size_t)n < sizeof(result); ++i) {
            if (gui.calls[i].id != 0) {
                aos_text_from_utf8(gui.calls[i].peer, c, sizeof(c));
                n += snprintf(result + n, sizeof(result) - (size_t)n, "|call %d %s %s", gui.calls[i].id,
                              call_state_name(gui.calls[i].state), c);
            }
        }
        break;
    }
    case RX_QUIT: DoMethod(gui.app, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit); break;
    }
    if (!ok) {
        snprintf(result, sizeof(result), "ERROR");
    }
    rexx_result(result);
    return ok ? RC_OK : RC_ERROR;
}

/* clang-format off */
AROS_UFH3(static IPTR, rexx_entry,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(Object *, app, A2),
    AROS_UFHA(IPTR *, args, A1)) {
    AROS_USERFUNC_INIT
    return DoMethod(app, MUIM_Phone_Rexx, (IPTR)hook->h_Data, (IPTR)args);
    AROS_USERFUNC_EXIT
}

BOOPSI_DISPATCHER(IPTR, phone_dispatcher, cl, obj, msg)
    struct phone_msg *pmsg = (struct phone_msg *)msg;
    switch (msg->MethodID) {
    case MUIM_Phone_CoreEvents: return do_core_events();
    case MUIM_Phone_Tick: return do_tick();
    case MUIM_Phone_Register: return do_register();
    case MUIM_Phone_Unregister: return do_unregister();
    case MUIM_Phone_Call: return do_call();
    case MUIM_Phone_Answer: return do_answer();
    case MUIM_Phone_Hangup: return do_hangup();
    case MUIM_Phone_OpenChat: return do_open_chat();
    case MUIM_Phone_SendChat: return do_send_chat((int)pmsg->arg1);
    case MUIM_Phone_ShowCall: return do_show_call();
    case MUIM_Phone_Rexx: return do_rexx((int)pmsg->arg1, (IPTR *)pmsg->arg2);
    }
    return DoSuperMethodA(cl, obj, msg);
BOOPSI_DISPATCHER_END
/* clang-format on */

/* ---- creation ---- */

static Object *create_windows(void) {
    Object *account_button, *call_button, *chat_button, *register_button, *unregister_button;

    /* clang-format off */
    gui.main_win = WindowObject,
        MUIA_Window_Title, (IPTR)"aos-phone",
        MUIA_Window_ID, MAKE_ID('A', 'P', 'M', 'W'),
        MUIA_Window_Width, MUIV_Window_Width_Screen(35),
        WindowContents, VGroup,
            Child, HGroup, GroupFrameT("Account"),
                Child, gui.account_text = TextObject, TextFrame, MUIA_Background, MUII_TextBack,
                    MUIA_Text_Contents, (IPTR)"no account",
                End,
                Child, account_button = SimpleButton("_Account..."),
            End,
            Child, HGroup, GroupFrameT("Address"),
                Child, gui.address_str = StringObject, StringFrame,
                    MUIA_String_MaxLen, AOS_CORE_ADDR_MAX,
                    MUIA_CycleChain, 1,
                End,
                Child, call_button = SimpleButton("_Call"),
                Child, chat_button = SimpleButton("C_hat"),
            End,
            Child, VGroup, GroupFrameT("Calls"),
                Child, gui.calls_view = ListviewObject,
                    MUIA_Listview_List, gui.calls_list = ListObject,
                        InputListFrame,
                        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                        MUIA_List_DestructHook, MUIV_List_DestructHook_String,
                    End,
                End,
                Child, HGroup,
                    Child, gui.answer_button = SimpleButton("A_nswer"),
                    Child, gui.hangup_button = SimpleButton("Hang _up"),
                End,
            End,
            Child, gui.status_text = TextObject, TextFrame, MUIA_Background, MUII_TextBack,
                MUIA_Text_Contents, (IPTR)"",
            End,
        End,
    End;

    gui.account_win = WindowObject,
        MUIA_Window_Title, (IPTR)"aos-phone: account",
        MUIA_Window_ID, MAKE_ID('A', 'P', 'A', 'C'),
        MUIA_Window_Width, MUIV_Window_Width_Screen(40),
        WindowContents, VGroup,
            Child, ColGroup(2),
                Child, Label2("Identity:"),
                Child, gui.identity_str = StringObject, StringFrame,
                    MUIA_String_MaxLen, AOS_CORE_ADDR_MAX, MUIA_CycleChain, 1,
                End,
                Child, Label2("Password:"),
                Child, gui.password_str = StringObject, StringFrame,
                    MUIA_String_MaxLen, 128, MUIA_String_Secret, TRUE, MUIA_CycleChain, 1,
                End,
                Child, Label2("Server:"),
                Child, gui.server_str = StringObject, StringFrame,
                    MUIA_String_MaxLen, AOS_CORE_ADDR_MAX, MUIA_CycleChain, 1,
                End,
            End,
            Child, TextObject,
                MUIA_Text_Contents, (IPTR)"Identity: sip:user@domain. Server: empty for the domain,\n"
                                          "or e.g. sip:example.org:5060.",
            End,
            Child, HGroup,
                Child, register_button = SimpleButton("_Register"),
                Child, unregister_button = SimpleButton("_Unregister"),
            End,
        End,
    End;

    gui.call_win = WindowObject,
        MUIA_Window_Title, (IPTR)"aos-phone: call",
        MUIA_Window_ID, MAKE_ID('A', 'P', 'C', 'L'),
        MUIA_Window_Width, MUIV_Window_Width_Screen(30),
        WindowContents, VGroup,
            Child, gui.call_peer_text = TextObject, TextFrame, MUIA_Background, MUII_TextBack,
                MUIA_Text_PreParse, (IPTR)"\33c", MUIA_Text_Contents, (IPTR)"no call",
            End,
            Child, HGroup,
                Child, gui.call_state_text = TextObject, MUIA_Text_Contents, (IPTR)"", End,
                Child, gui.call_time_text = TextObject, MUIA_Text_PreParse, (IPTR)"\33r",
                    MUIA_Text_Contents, (IPTR)"",
                End,
            End,
            Child, gui.mic_gauge = GaugeObject, GaugeFrame,
                MUIA_Gauge_Horiz, TRUE, MUIA_Gauge_Max, 100, MUIA_Gauge_InfoText, (IPTR)"Microphone",
            End,
            Child, gui.speaker_gauge = GaugeObject, GaugeFrame,
                MUIA_Gauge_Horiz, TRUE, MUIA_Gauge_Max, 100, MUIA_Gauge_InfoText, (IPTR)"Speaker",
            End,
            Child, HGroup,
                Child, gui.call_answer_button = SimpleButton("_Answer"),
                Child, gui.call_hangup_button = SimpleButton("_Hang up"),
            End,
        End,
    End;
    /* clang-format on */

    if (gui.main_win == NULL || gui.account_win == NULL || gui.call_win == NULL) {
        return NULL;
    }

    for (int i = 0; i < RX_COUNT; ++i) {
        gui.rexx_hooks[i].h_Entry = (HOOKFUNC)rexx_entry;
        gui.rexx_hooks[i].h_Data = (APTR)(IPTR)i;
    }

    /* clang-format off */
    Object *app = NewObject(gui.phone_class->mcc_Class, NULL,
        MUIA_Application_Title, (IPTR)"aos-phone",
        MUIA_Application_Version, (IPTR)"$VER: aos-phone 0.1 (24.11.2026)",
        MUIA_Application_Description, (IPTR)"SIP phone",
        MUIA_Application_Base, (IPTR)"AOSPHONE",
        MUIA_Application_Commands, (IPTR)rexx_commands,
        SubWindow, (IPTR)gui.main_win,
        SubWindow, (IPTR)gui.account_win,
        SubWindow, (IPTR)gui.call_win,
        TAG_DONE);
    /* clang-format on */
    if (app == NULL) {
        /* A Zune object that fails to come up disposes its children. */
        gui.main_win = gui.account_win = gui.call_win = NULL;
        return NULL;
    }
    gui.app = app;

    DoMethod(gui.main_win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app, 2, MUIM_Application_ReturnID,
             MUIV_Application_ReturnID_Quit);
    DoMethod(account_button, MUIM_Notify, MUIA_Pressed, FALSE, gui.account_win, 3, MUIM_Set, MUIA_Window_Open, TRUE);
    DoMethod(gui.account_win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, MUIV_Notify_Self, 3, MUIM_Set,
             MUIA_Window_Open, FALSE);
    DoMethod(register_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Register);
    DoMethod(unregister_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Unregister);
    DoMethod(call_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Call);
    DoMethod(gui.address_str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app, 1, MUIM_Phone_Call);
    DoMethod(chat_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_OpenChat);
    DoMethod(gui.answer_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Answer);
    DoMethod(gui.call_answer_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Answer);
    DoMethod(gui.hangup_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Hangup);
    DoMethod(gui.call_hangup_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Hangup);
    DoMethod(gui.calls_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, app, 1, MUIM_Phone_ShowCall);
    DoMethod(gui.calls_view, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, gui.call_win, 3, MUIM_Set,
             MUIA_Window_Open, TRUE);
    DoMethod(gui.call_win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, MUIV_Notify_Self, 3, MUIM_Set,
             MUIA_Window_Open, FALSE);

    Object *cycle[] = {account_button, call_button, chat_button, gui.answer_button, gui.hangup_button,
                       register_button, unregister_button, gui.call_answer_button, gui.call_hangup_button};
    for (size_t i = 0; i < sizeof(cycle) / sizeof(cycle[0]); ++i) {
        set(cycle[i], MUIA_CycleChain, 1);
    }
    return app;
}

int aos_gui_create(void) {
    gui.task = FindTask(NULL);
    gui.signal = AllocSignal(-1);
    if (gui.signal < 0) {
        return -1;
    }
    gui.phone_class = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Application, NULL, sizeof(IPTR), phone_dispatcher);
    if (gui.phone_class == NULL || create_windows() == NULL) {
        aos_gui_dispose();
        return -1;
    }

    gui.events_ihn.ihn_Object = gui.app;
    gui.events_ihn.ihn_Signals = 1UL << gui.signal;
    gui.events_ihn.ihn_Flags = 0;
    gui.events_ihn.ihn_Method = MUIM_Phone_CoreEvents;
    gui.timer_ihn.ihn_Object = gui.app;
    gui.timer_ihn.ihn_Millis = 1000;
    gui.timer_ihn.ihn_Flags = MUIIHNF_TIMER;
    gui.timer_ihn.ihn_Method = MUIM_Phone_Tick;
    DoMethod(gui.app, MUIM_Application_AddInputHandler, (IPTR)&gui.events_ihn);
    DoMethod(gui.app, MUIM_Application_AddInputHandler, (IPTR)&gui.timer_ihn);
    gui.handlers_added = 1;

    show_call_window();
    set(gui.main_win, MUIA_Window_Open, TRUE);
    return 0;
}

void aos_gui_run(void) {
    ULONG signals = 0;
    while ((LONG)DoMethod(gui.app, MUIM_Application_NewInput, (IPTR)&signals) !=
           (LONG)MUIV_Application_ReturnID_Quit) {
        if (signals != 0) {
            signals = Wait(signals | SIGBREAKF_CTRL_C);
            if (signals & SIGBREAKF_CTRL_C) {
                break;
            }
        }
    }
}

void aos_gui_dispose(void) {
    if (gui.app != NULL) {
        if (gui.handlers_added) {
            DoMethod(gui.app, MUIM_Application_RemInputHandler, (IPTR)&gui.timer_ihn);
            DoMethod(gui.app, MUIM_Application_RemInputHandler, (IPTR)&gui.events_ihn);
            gui.handlers_added = 0;
        }
        MUI_DisposeObject(gui.app);
        gui.app = NULL;
    } else {
        /* Windows that never joined an application. */
        if (gui.main_win != NULL) {
            MUI_DisposeObject(gui.main_win);
        }
        if (gui.account_win != NULL) {
            MUI_DisposeObject(gui.account_win);
        }
        if (gui.call_win != NULL) {
            MUI_DisposeObject(gui.call_win);
        }
    }
    gui.main_win = gui.account_win = gui.call_win = NULL;
    if (gui.phone_class != NULL) {
        MUI_DeleteCustomClass(gui.phone_class);
        gui.phone_class = NULL;
    }
    if (gui.signal >= 0) {
        FreeSignal(gui.signal);
        gui.signal = -1;
    }
}

void aos_gui_notify(void *context) {
    (void)context;
    Signal(gui.task, 1UL << gui.signal);
}
