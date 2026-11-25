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
 *
 * Video pictures do not travel through the core events: the display filter
 * leaves them on a surface (video.c) and wakes the interface with a second
 * signal, after which the video areas draw the newest picture.
 */
#include "gui.h"
#include "core.h"
#include "lamp.h"
#include "text.h"
#include "video.h"

#include <exec/types.h>
#include <libraries/mui.h>
#include <rexx/errors.h>
#include <utility/hooks.h>

#include <clib/alib_protos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_CHATS      8
#define CHAT_LINES_MAX 200
#define INPUT_MAX      512 /* bytes a user types into a line, in the code page */
#define LINE_MAX_LEN   (AOS_CORE_TEXT_MAX + AOS_CORE_ADDR_MAX + 32)
#define STATUS_MAX     (AOS_CORE_TEXT_MAX + 64)

#define PHONE_METHOD(n)          (TAG_USER | 0x3a5a0000 | (n))
#define MUIM_Phone_CoreEvents    PHONE_METHOD(1)
#define MUIM_Phone_Tick          PHONE_METHOD(2)
#define MUIM_Phone_Register      PHONE_METHOD(3)
#define MUIM_Phone_Unregister    PHONE_METHOD(4)
#define MUIM_Phone_Call          PHONE_METHOD(5)
#define MUIM_Phone_Answer        PHONE_METHOD(6)
#define MUIM_Phone_Hangup        PHONE_METHOD(7)
#define MUIM_Phone_OpenChat      PHONE_METHOD(8)
#define MUIM_Phone_SendChat      PHONE_METHOD(9)
#define MUIM_Phone_ShowCall      PHONE_METHOD(10)
#define MUIM_Phone_Rexx          PHONE_METHOD(11)
#define MUIM_Phone_Video         PHONE_METHOD(12)
#define MUIM_Phone_SetVideo      PHONE_METHOD(13)
#define MUIM_Phone_Ring          PHONE_METHOD(14)
#define MUIM_Phone_PickAccount   PHONE_METHOD(15)
#define MUIM_Phone_NewAccount    PHONE_METHOD(16)
#define MUIM_Phone_RemoveAccount PHONE_METHOD(17)
#define MUIM_Phone_UseAccount    PHONE_METHOD(18)
#define MUIM_Phone_AccountForm   PHONE_METHOD(19)
#define MUIM_Phone_CloseError    PHONE_METHOD(20)

#define MAX_ACCOUNTS 8

#define RING_FRAME_MS 120 /* animation step of the ringing lamp */

/* Seconds a registration started from the account window is watched for
   its result, longer than a SIP transaction may last (32 s). */
#define WATCH_SECONDS 60

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
    RX_PLAY,
    RX_SNAPSHOT,
    RX_VIDEO,
    RX_SEND,
    RX_STATUS,
    RX_ACCOUNTS,
    RX_REMOVEACCOUNT,
    RX_USEACCOUNT,
    RX_QUIT,
    RX_COUNT
};

/* An account as the core reported it last. */
struct gui_account {
    char identity[AOS_CORE_ADDR_MAX]; /* UTF-8 */
    char server[AOS_CORE_ADDR_MAX];   /* UTF-8 */
    int state;                        /* enum aos_reg_state */
    int is_default;                   /* calls go out through it */
};

struct gui_call {
    int id; /* 0: free */
    int state;
    int video;
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
    struct MUI_InputHandlerNode ring_ihn; /* only while a call rings */
    int ring_running;
    ULONG ring_frame;
    struct MUI_InputHandlerNode video_ihn;
    BYTE video_signal;
    int handlers_added;

    struct MUI_CustomClass *video_class;
    struct MUI_CustomClass *lamp_class;
    Object *account_lamp; /* state of the registration */
    Object *ring_lamp;    /* animated while the call in the call window rings */
    struct aos_video_surface *surface;
    Object *video_group, *remote_video, *preview_video;
    int video_shown; /* the video group is shown */

    Object *main_win, *account_win, *call_win;
    Object *account_text, *address_str, *calls_view, *calls_list, *status_text;
    Object *answer_button, *hangup_button;
    Object *identity_str, *password_str, *server_str;
    Object *domain_text; /* "@host" after the identity, from the server */
    Object *error_win, *error_text, *error_ok;
    Object *call_peer_text, *call_state_text, *call_time_text, *mic_gauge, *speaker_gauge;
    Object *call_answer_button, *call_hangup_button, *call_video_button;

    struct gui_call calls[AOS_CORE_MAX_CALLS];
    int rows[AOS_CORE_MAX_CALLS]; /* call id of each row of the call list */
    struct gui_call shown;        /* the call in the call window; id 0: none */
    struct chat chats[MAX_CHATS];
    unsigned use_counter;
    int sip_port;
    struct gui_account accounts[MAX_ACCOUNTS];
    int account_count;
    Object *accounts_list;
    /* Identity registered from the account window (UTF-8, as sent to the
       core) whose result shows in the error window if it fails; watch_left
       counts the seconds until it is no longer watched. */
    char watch[AOS_CORE_ADDR_MAX];
    int watch_left;
    struct Hook rexx_hooks[RX_COUNT];
} gui;

/* Zune compares the command word over its own length, so no name may
   start with an earlier, shorter one. */
static struct MUI_Command rexx_commands[] = {
    {"REGISTER", "IDENTITY/A,PASSWORD/A,SERVER", 3, &gui.rexx_hooks[RX_REGISTER], {0}},
    {"UNREGISTER", "IDENTITY", 1, &gui.rexx_hooks[RX_UNREGISTER], {0}},
    {"CALL", "ADDRESS/A", 1, &gui.rexx_hooks[RX_CALL], {0}},
    {"ANSWER", NULL, 0, &gui.rexx_hooks[RX_ANSWER], {0}},
    {"HANGUP", NULL, 0, &gui.rexx_hooks[RX_HANGUP], {0}},
    {"PLAY", "FILE/A/F", 1, &gui.rexx_hooks[RX_PLAY], {0}},
    {"SNAPSHOT", "FILE/A/F", 1, &gui.rexx_hooks[RX_SNAPSHOT], {0}},
    {"VIDEO", "ON/S,OFF/S", 2, &gui.rexx_hooks[RX_VIDEO], {0}},
    {"SEND", "TO/A,TEXT/A/F", 2, &gui.rexx_hooks[RX_SEND], {0}},
    {"STATUS", NULL, 0, &gui.rexx_hooks[RX_STATUS], {0}},
    {"ACCOUNTS", NULL, 0, &gui.rexx_hooks[RX_ACCOUNTS], {0}},
    {"REMOVEACCOUNT", "IDENTITY/A", 1, &gui.rexx_hooks[RX_REMOVEACCOUNT], {0}},
    {"USEACCOUNT", "IDENTITY/A", 1, &gui.rexx_hooks[RX_USEACCOUNT], {0}},
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

/* Shows a problem the user has to see in the error window, over the
   account window when that is open, and in the status line. The window
   does not stop the interface; a newer problem replaces the one shown. */
static void show_error(const char *format, ...) {
    char text[STATUS_MAX];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    char line[STATUS_MAX];
    copy_text(line, sizeof(line), text);
    for (char *c = line; *c != '\0'; ++c) {
        if (*c == '\n') {
            *c = ' ';
        }
    }
    status("%s", line);

    char shown[STATUS_MAX];
    aos_text_from_utf8(text, shown, sizeof(shown));
    for (char *c = shown; *c != '\0'; ++c) {
        if ((unsigned char)*c < 0x20 && *c != '\n') {
            *c = ' ';
        }
    }
    IPTR open = FALSE, account_open = FALSE;
    get(gui.error_win, MUIA_Window_Open, &open);
    get(gui.account_win, MUIA_Window_Open, &account_open);
    if (open) {
        set(gui.error_win, MUIA_Window_Open, FALSE); /* to fit the new text */
    }
    set(gui.error_win, MUIA_Window_RefWindow, (IPTR)(account_open ? gui.account_win : gui.main_win));
    set_text(gui.error_text, shown);
    set(gui.error_win, MUIA_Window_Open, TRUE);
    set(gui.error_win, MUIA_Window_ActiveObject, (IPTR)gui.error_ok);
}

/* The state to show for one the core reported: a renewal stays
   registered when the account was, and counts as registering otherwise
   (after a failure, or with no state known). */
static int shown_reg_state(int state, int before) {
    if (state == AOS_REG_REFRESHING) {
        return before == AOS_REG_OK ? AOS_REG_OK : AOS_REG_PROGRESS;
    }
    return state;
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

/* Copies text without the blanks around it. */
static void trim_copy(char *dst, size_t size, const char *src) {
    while (*src == ' ' || *src == '\t') {
        ++src;
    }
    size_t len = strlen(src);
    while (len > 0 && (src[len - 1] == ' ' || src[len - 1] == '\t')) {
        --len;
    }
    if (len >= size) {
        len = size - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* Whether the first n bytes of two texts match, ignoring the case of
   ASCII letters. */
static int same_n(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
        if (a[i] == '\0') {
            return 1;
        }
    }
    return 1;
}

/* Length of the "sip:" or "sips:" an address starts with, in any case; 0
   when it has none. */
static size_t scheme_len(const char *address) {
    if (same_n(address, "sip:", 4)) {
        return 4;
    }
    if (same_n(address, "sips:", 5)) {
        return 5;
    }
    return 0;
}

/* The domain identities get from a server address typed with or without
   a scheme: its host and port, without a user part and parameters, e.g.
   example.org:5062 of sip:example.org:5062;transport=tcp. */
static void server_domain(const char *server, char *domain, size_t size) {
    const char *p = server + scheme_len(server);
    const char *at = strchr(p, '@');
    if (at != NULL && (size_t)(at - p) < strcspn(p, ";?>")) {
        p = at + 1;
    }
    size_t len;
    if (*p == '[') { /* an IPv6 address */
        len = strcspn(p, "]");
        len += p[len] == ']';
    } else {
        len = strcspn(p, ":;?>/ \t");
    }
    if (p[len] == ':' && isdigit((unsigned char)p[len + 1])) {
        ++len;
        while (isdigit((unsigned char)p[len])) {
            ++len;
        }
    }
    if (len >= size) {
        len = size - 1;
    }
    memcpy(domain, p, len);
    domain[len] = '\0';
}

/* Builds the addresses for the core from the fields of the account window,
   which hold them without "sip:": the server gets "sip:" in front, and a
   user name becomes sip:user@<host and port of the server>; user@domain
   only gets "sip:". Text typed with a scheme stays as it is. Returns NULL,
   or what the user has to correct. */
static const char *compose_account(const char *identity_field, const char *server_field, char *identity,
                                   size_t identity_size, char *server, size_t server_size) {
    char typed[AOS_CORE_ADDR_MAX], host[AOS_CORE_ADDR_MAX];
    trim_copy(typed, sizeof(typed), server_field);
    int n = snprintf(server, server_size, "%s%s", typed[0] != '\0' && scheme_len(typed) == 0 ? "sip:" : "", typed);
    if (n < 0 || (size_t)n >= server_size) {
        return "The server address is too long.";
    }
    server_domain(typed, host, sizeof(host));

    trim_copy(typed, sizeof(typed), identity_field);
    size_t scheme = scheme_len(typed);
    const char *user = typed + scheme;
    if (user[0] == '\0' || user[0] == '@') {
        return "Enter the user name of the account,\ne.g. alice.";
    }
    if (strchr(user, '@') != NULL) {
        n = snprintf(identity, identity_size, "%s%s", scheme == 0 ? "sip:" : "", typed);
    } else if (host[0] == '\0') {
        return "Enter the server, e.g. sip.linphone.org;\nthe identity takes its domain from it.";
    } else {
        n = snprintf(identity, identity_size, "%.*s%s@%s", (int)(scheme != 0 ? scheme : 4),
                     scheme != 0 ? typed : "sip:", user, host);
    }
    if (n < 0 || (size_t)n >= identity_size) {
        return "The identity is too long.";
    }
    return NULL;
}

/* Whether two identities name the same account: the same user@host,
   ignoring the scheme, parameters and the case of ASCII letters. */
static int same_identity(const char *a, const char *b) {
    a += scheme_len(a);
    b += scheme_len(b);
    size_t len = strcspn(a, ";>");
    return len == strcspn(b, ";>") && same_n(a, b, len);
}

/* Shows the domain the identity gets after its field: the host and port
   of the server, or nothing when the identity names its own domain. */
static IPTR do_account_form(void) {
    STRPTR identity = NULL, server = NULL;
    get(gui.identity_str, MUIA_String_Contents, &identity);
    get(gui.server_str, MUIA_String_Contents, &server);
    char host[AOS_CORE_ADDR_MAX], suffix[AOS_CORE_ADDR_MAX + 1];
    suffix[0] = '\0';
    if (identity == NULL || strchr((const char *)identity, '@') == NULL) {
        server_domain(server != NULL ? (const char *)server : "", host, sizeof(host));
        if (host[0] != '\0') {
            snprintf(suffix, sizeof(suffix), "@%s", host);
        }
    }
    strip_controls(suffix);
    STRPTR before = NULL;
    get(gui.domain_text, MUIA_Text_Contents, &before);
    if (before == NULL || strcmp((const char *)before, suffix) != 0) {
        set_text(gui.domain_text, suffix);
    }
    return 0;
}

/* Fills the fields with an account (UTF-8) the way they are typed: the
   server without "sip:", and only the user name when the identity is in
   the domain of the server. */
static void fill_form(const char *identity, const char *server) {
    char text[AOS_CORE_ADDR_MAX], shown[AOS_CORE_ADDR_MAX], host[AOS_CORE_ADDR_MAX];
    to_display(server + (scheme_len(server) == 4 ? 4 : 0), shown, sizeof(shown));
    set(gui.server_str, MUIA_String_Contents, (IPTR)shown);

    const char *id = identity + (scheme_len(identity) == 4 ? 4 : 0);
    copy_text(text, sizeof(text), id);
    const char *at = strchr(id, '@');
    server_domain(server, host, sizeof(host));
    if (at != NULL && host[0] != '\0') {
        size_t domain_len = strcspn(at + 1, ";>?");
        if (domain_len == strlen(host) && same_n(at + 1, host, domain_len)) {
            text[at - id] = '\0';
        }
    }
    to_display(text, shown, sizeof(shown));
    set(gui.identity_str, MUIA_String_Contents, (IPTR)shown);
    set(gui.password_str, MUIA_String_Contents, (IPTR) "");
    do_account_form();
}

static const struct gui_account *default_account(void) {
    for (int i = 0; i < gui.account_count; ++i) {
        if (gui.accounts[i].is_default) {
            return &gui.accounts[i];
        }
    }
    return gui.account_count > 0 ? &gui.accounts[0] : NULL;
}

static struct gui_account *find_account(const char *identity) {
    for (int i = 0; i < gui.account_count; ++i) {
        if (strcmp(gui.accounts[i].identity, identity) == 0) {
            return &gui.accounts[i];
        }
    }
    return NULL;
}

/* Green when registered, yellow while registering, red otherwise: no
   account, the core not running, or the server out of reach or refusing.
   The lamp and the line show the default account, which calls go out
   through. */
static void show_lamp(const struct gui_account *account) {
    ULONG colour = AOS_LAMP_RED;
    const char *help = "no account";
    if (account != NULL) {
        colour = account->state == AOS_REG_OK         ? AOS_LAMP_GREEN
                 : account->state == AOS_REG_PROGRESS ? AOS_LAMP_YELLOW
                                                      : AOS_LAMP_RED;
        help = reg_name(account->state); /* Zune keeps the pointer */
    }
    SetAttrs(gui.account_lamp, AOS_LAMP_COLOUR, colour, MUIA_ShortHelp, (IPTR)help, TAG_DONE);
}

static void show_account(void) {
    char line[AOS_CORE_ADDR_MAX + 64], shown[sizeof(line)];
    const struct gui_account *account = default_account();
    if (account == NULL) {
        snprintf(line, sizeof(line), "no account");
    } else if (gui.account_count > 1) {
        snprintf(line, sizeof(line), "%s: %s (%d accounts)", account->identity, reg_name(account->state),
                 gui.account_count);
    } else {
        snprintf(line, sizeof(line), "%s: %s", account->identity, reg_name(account->state));
    }
    to_display(line, shown, sizeof(shown));
    set_text(gui.account_text, shown);
    show_lamp(account);
}

/* The list in the account window, the default account in bold, and the
   line of the main window. */
static void show_accounts(void) {
    IPTR active = MUIV_List_Active_Off;
    get(gui.accounts_list, MUIA_List_Active, &active);
    set(gui.accounts_list, MUIA_List_Quiet, TRUE);
    DoMethod(gui.accounts_list, MUIM_List_Clear);
    for (int i = 0; i < gui.account_count; ++i) {
        const struct gui_account *account = &gui.accounts[i];
        char line[AOS_CORE_ADDR_MAX + 64], shown[sizeof(line) + 2];
        snprintf(line, sizeof(line), "%s: %s%s", account->identity, reg_name(account->state),
                 account->is_default ? ", for calls" : "");
        /* The MUI escape goes in after to_display(), which keeps only
           printable characters. */
        shown[0] = '\33';
        shown[1] = 'b';
        to_display(line, shown + 2, sizeof(shown) - 2);
        DoMethod(gui.accounts_list, MUIM_List_InsertSingle, (IPTR)(account->is_default ? shown : shown + 2),
                 MUIV_List_Insert_Bottom);
    }
    set(gui.accounts_list, MUIA_List_Quiet, FALSE);
    if ((LONG)active >= 0 && (LONG)active < gui.account_count) {
        nnset(gui.accounts_list, MUIA_List_Active, active);
    }
    show_account();
}

/* The account selected in the list, else the one in the fields; empty
   when the fields name none. */
static void chosen_identity(char *identity, size_t size) {
    IPTR active = MUIV_List_Active_Off;
    get(gui.accounts_list, MUIA_List_Active, &active);
    if ((LONG)active >= 0 && (LONG)active < gui.account_count) {
        copy_text(identity, size, gui.accounts[active].identity);
        return;
    }
    char typed_identity[AOS_CORE_ADDR_MAX], typed_server[AOS_CORE_ADDR_MAX], server[AOS_CORE_ADDR_MAX];
    string_utf8(gui.identity_str, typed_identity, sizeof(typed_identity));
    string_utf8(gui.server_str, typed_server, sizeof(typed_server));
    if (compose_account(typed_identity, typed_server, identity, size, server, sizeof(server)) != NULL) {
        identity[0] = '\0';
    }
}

/* Registers an account as typed into the fields or given to REGISTER, in
   the system code page. From the window, a problem shows in the error
   window, and so does a failure of the registration (watched in
   on_event()); ARexx gets ERROR and the status line. */
static IPTR register_account(const char *typed_identity, const char *typed_password, const char *typed_server,
                             int from_window) {
    char typed_id[AOS_CORE_ADDR_MAX], typed_srv[AOS_CORE_ADDR_MAX];
    char identity[AOS_CORE_ADDR_MAX], server[AOS_CORE_ADDR_MAX], password[128];
    aos_text_to_utf8(typed_identity, typed_id, sizeof(typed_id));
    aos_text_to_utf8(typed_server, typed_srv, sizeof(typed_srv));
    const char *problem = compose_account(typed_id, typed_srv, identity, sizeof(identity), server, sizeof(server));
    if (problem != NULL) {
        if (from_window) {
            show_error("%s", problem);
        } else {
            status("%s", problem);
        }
        return FALSE;
    }
    aos_text_to_utf8(typed_password, password, sizeof(password));
    int rc = aos_core_register(identity, password, server);
    memset(password, 0, sizeof(password));
    if (rc != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    if (from_window) {
        copy_text(gui.watch, sizeof(gui.watch), identity);
        gui.watch_left = WATCH_SECONDS;
    }
    fill_form(identity, server); /* also empties the password field */
    status("registering %s", identity);
    return TRUE;
}

static IPTR do_register(void) {
    STRPTR identity = NULL, password = NULL, server = NULL;
    get(gui.identity_str, MUIA_String_Contents, &identity);
    get(gui.password_str, MUIA_String_Contents, &password);
    get(gui.server_str, MUIA_String_Contents, &server);
    return register_account(identity != NULL ? (const char *)identity : "",
                            password != NULL ? (const char *)password : "",
                            server != NULL ? (const char *)server : "", TRUE);
}

static IPTR do_unregister(const char *identity) {
    if (aos_core_unregister(identity) != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    return TRUE;
}

/* Fills the fields with the account picked in the list; the password stays
   empty, which keeps the saved one when registering again. */
static IPTR do_pick_account(void) {
    IPTR active = MUIV_List_Active_Off;
    get(gui.accounts_list, MUIA_List_Active, &active);
    if ((LONG)active < 0 || (LONG)active >= gui.account_count) {
        return 0;
    }
    fill_form(gui.accounts[active].identity, gui.accounts[active].server);
    return 0;
}

static IPTR do_new_account(void) {
    set(gui.accounts_list, MUIA_List_Active, MUIV_List_Active_Off);
    set(gui.server_str, MUIA_String_Contents, (IPTR) "");
    set(gui.identity_str, MUIA_String_Contents, (IPTR) "");
    set(gui.password_str, MUIA_String_Contents, (IPTR) "");
    set(gui.account_win, MUIA_Window_ActiveObject, (IPTR)gui.server_str);
    do_account_form();
    return 0;
}

/* Removes an account; from the window only after the user confirms. */
static IPTR remove_account(const char *identity, int ask) {
    if (identity[0] == '\0' || find_account(identity) == NULL) {
        status("pick the account to remove");
        return FALSE;
    }
    if (ask) {
        char shown[AOS_CORE_ADDR_MAX];
        to_display(identity, shown, sizeof(shown));
        if (MUI_Request(gui.app, gui.account_win, 0, (CONST_STRPTR) "aos-phone", (CONST_STRPTR) "_Remove|_Cancel",
                        (CONST_STRPTR) "Remove the account\n%s?\n\nIts settings and password are deleted.",
                        (IPTR)shown) != 1) {
            return FALSE;
        }
    }
    if (aos_core_remove_account(identity) != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    status("removing %s", identity);
    return TRUE;
}

static IPTR use_account(const char *identity) {
    if (identity[0] == '\0' || find_account(identity) == NULL) {
        status("pick the account to call through");
        return FALSE;
    }
    if (aos_core_set_default_account(identity) != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    return TRUE;
}

static IPTR do_remove_account(void) {
    char identity[AOS_CORE_ADDR_MAX];
    chosen_identity(identity, sizeof(identity));
    if (!remove_account(identity, TRUE)) {
        return FALSE;
    }
    do_new_account(); /* the fields showed the removed account */
    return TRUE;
}

static IPTR do_use_account(void) {
    char identity[AOS_CORE_ADDR_MAX];
    chosen_identity(identity, sizeof(identity));
    return use_account(identity);
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

/* Shows and animates the ringing lamp, with its timer running only then. */
static void show_ring(int ringing) {
    if (gui.ring_lamp == NULL || !gui.handlers_added || ringing == gui.ring_running) {
        return;
    }
    if (ringing) {
        gui.ring_frame = 0;
        set(gui.ring_lamp, AOS_LAMP_FRAME, 0);
        DoMethod(gui.app, MUIM_Application_AddInputHandler, (IPTR)&gui.ring_ihn);
    } else {
        DoMethod(gui.app, MUIM_Application_RemInputHandler, (IPTR)&gui.ring_ihn);
    }
    gui.ring_running = ringing;
    set(gui.ring_lamp, MUIA_ShowMe, ringing ? TRUE : FALSE);
}

static IPTR do_ring(void) {
    gui.ring_frame = (gui.ring_frame + 1) % AOS_LAMP_FRAMES;
    set(gui.ring_lamp, AOS_LAMP_FRAME, gui.ring_frame);
    return 0;
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
        /* An incoming call in bold. The MUI escape goes in after to_display(),
           which keeps only printable characters. */
        shown[0] = '\33';
        shown[1] = 'b';
        to_display(line, shown + 2, sizeof(shown) - 2);
        set_text(gui.call_state_text, call->state == AOS_CALL_INCOMING ? shown : shown + 2);
        format_duration(call, line, sizeof(line));
        set_text(gui.call_time_text, line);
    }
    if (call->id == 0 || call->state != AOS_CALL_CONNECTED) {
        set(gui.mic_gauge, MUIA_Gauge_Current, 0);
        set(gui.speaker_gauge, MUIA_Gauge_Current, 0);
    }
    int video = call->id != 0 && call->state == AOS_CALL_CONNECTED && call->video;
    if (video != gui.video_shown && gui.video_group != NULL) {
        gui.video_shown = video;
        set(gui.video_group, MUIA_ShowMe, video);
    }
    show_ring(call->id != 0 && call->state == AOS_CALL_INCOMING);
    BOOL can_answer = call->id != 0 && call->state == AOS_CALL_INCOMING;
    BOOL can_hangup = call->id != 0 && !call_final(call->state);
    /* nnset: showing the state must not ask the core to change it. */
    nnset(gui.call_video_button, MUIA_Selected, video ? TRUE : FALSE);
    set(gui.call_video_button, MUIA_Disabled, call->id == 0 || call->state != AOS_CALL_CONNECTED);
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
    update.video = event->video;
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

/* Plays a file to the call in the call window, or else the current call. */
static IPTR do_play(const char *file) {
    int id = gui.shown.id != 0 && gui.shown.state == AOS_CALL_CONNECTED ? gui.shown.id : 0;
    if (aos_core_play(id, file) != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    return TRUE;
}

/* Saves the next received picture of the call in the call window, or else
   of the current call. */
static IPTR do_snapshot(const char *file) {
    int id = gui.shown.id != 0 && gui.shown.state == AOS_CALL_CONNECTED ? gui.shown.id : 0;
    if (aos_core_snapshot(id, file) != 0) {
        status("the core is busy; try again");
        return FALSE;
    }
    return TRUE;
}

/* Turns the video of the call in the call window, or else of the current
   call, on or off. The button shows the outcome once the call reports it. */
static IPTR do_set_video(int on) {
    int id = gui.shown.id != 0 && gui.shown.state == AOS_CALL_CONNECTED ? gui.shown.id : 0;
    if (aos_core_set_video(id, on) != 0) {
        status("the core is busy; try again");
        show_call_window();
        return FALSE;
    }
    status(on ? "asking for video" : "turning video off");
    return TRUE;
}

static IPTR do_video(void) {
    if (gui.remote_video != NULL) {
        aos_video_area_update(gui.remote_video);
        aos_video_area_update(gui.preview_video);
    }
    return 0;
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
    if (gui.watch_left > 0 && --gui.watch_left == 0) {
        gui.watch[0] = '\0';
    }
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
        show_account(); /* the list of accounts follows */
        break;
    case AOS_EVENT_START_FAILED:
        status("the linphone core could not start; check the network and PROGDIR:share");
        break;
    case AOS_EVENT_ACCOUNT: {
        struct gui_account *account = find_account(event->peer);
        int state = shown_reg_state(event->state, account != NULL ? account->state : AOS_REG_NONE);
        if (account != NULL) {
            account->state = state;
            if (event->id) {
                for (int i = 0; i < gui.account_count; ++i) {
                    gui.accounts[i].is_default = &gui.accounts[i] == account;
                }
            }
            show_accounts();
        }
        status("account %s: %s%s%s", event->peer, reg_name(state), event->text[0] ? " - " : "", event->text);
        /* The result of a registration started from the window. A server
           change unregisters first and a renewal comes before the answer,
           so only success or failure ends the watch. */
        if (gui.watch[0] != '\0' && same_identity(event->peer, gui.watch) &&
            (event->state == AOS_REG_OK || event->state == AOS_REG_FAILED)) {
            gui.watch[0] = '\0';
            gui.watch_left = 0;
            if (event->state == AOS_REG_FAILED) {
                const char *reason = event->text[0] != '\0' ? event->text : "no reason given";
                if (strcmp(reason, "io error") == 0) {
                    reason = "the server cannot be reached";
                }
                show_error("The registration of\n%s\nfailed: %s.\n\nCheck the server, the user name and the password.",
                           event->peer, reason);
            }
        }
        break;
    }
    case AOS_EVENT_ACCOUNTS:
        gui.account_count = 0;
        show_accounts();
        break;
    case AOS_EVENT_ACCOUNT_INFO:
        if (gui.account_count < MAX_ACCOUNTS) {
            struct gui_account *account = &gui.accounts[gui.account_count++];
            copy_text(account->identity, sizeof(account->identity), event->peer);
            copy_text(account->server, sizeof(account->server), event->text);
            account->state = shown_reg_state(event->state, AOS_REG_NONE);
            account->is_default = event->id;
            show_accounts();
        }
        break;
    case AOS_EVENT_CALL: {
        const struct gui_call *before = find_call(event->id);
        int was_connected = before != NULL && before->state == AOS_CALL_CONNECTED;
        printf("aos-phone: call %d %s%s: %s (%s)\n", event->id, call_state_name(event->state),
               event->video ? " with video" : "", event->peer, event->text);
        on_call_event(event);
        if (event->state == AOS_CALL_INCOMING) {
            status("incoming call from %s", event->peer);
        } else if (event->state == AOS_CALL_CONNECTED && was_connected) {
            status("video %s with %s", event->video ? "on" : "off", event->peer);
        } else if (event->state == AOS_CALL_CONNECTED) {
            status("connected with %s%s", event->peer, event->video ? ", with video" : "");
        } else if (call_final(event->state)) {
            status("call with %s %s%s%s", event->peer, call_state_name(event->state), event->text[0] ? ": " : "",
                   event->text);
        }
        break;
    }
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
    case AOS_EVENT_RECORDING: {
        /* status() takes UTF-8; the file name is in the system code page. */
        char file[AOS_CORE_TEXT_MAX];
        aos_text_to_utf8(event->text, file, sizeof(file));
        status("recording call %d to %s", event->id, file);
        break;
    }
    case AOS_EVENT_SNAPSHOT: {
        char file[AOS_CORE_TEXT_MAX];
        aos_text_to_utf8(event->text, file, sizeof(file));
        status("snapshot of call %d to %s", event->id, file);
        break;
    }
    case AOS_EVENT_ERROR:
        if (event->peer[0] != '\0' && gui.watch[0] != '\0' && strcmp(event->peer, gui.watch) == 0) {
            gui.watch[0] = '\0';
            gui.watch_left = 0;
            show_error("The account\n%s\ncannot be registered: %s.", event->peer, event->text);
        } else {
            status("%s", event->text);
        }
        show_call_window(); /* e.g. the Video button back to the state of the call */
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
        /* The same path as the Register button; the fields show the account. */
        ok = register_account((const char *)args[0], (const char *)args[1],
                              args[2] != 0 ? (const char *)args[2] : "", FALSE);
        break;
    case RX_UNREGISTER:
        aos_text_to_utf8(args[0] != 0 ? (const char *)args[0] : "", a, sizeof(a));
        ok = do_unregister(a);
        break;
    case RX_REMOVEACCOUNT:
        aos_text_to_utf8((const char *)args[0], a, sizeof(a));
        ok = remove_account(a, FALSE);
        break;
    case RX_USEACCOUNT:
        aos_text_to_utf8((const char *)args[0], a, sizeof(a));
        ok = use_account(a);
        break;
    case RX_ACCOUNTS: {
        int n = 0;
        result[0] = '\0';
        for (int i = 0; i < gui.account_count && n >= 0 && (size_t)n < sizeof(result); ++i) {
            aos_text_from_utf8(gui.accounts[i].identity, c, sizeof(c));
            n += snprintf(result + n, sizeof(result) - (size_t)n, "%s%s %s%s", i > 0 ? "|" : "", c,
                          reg_name(gui.accounts[i].state), gui.accounts[i].is_default ? " default" : "");
        }
        if (gui.account_count == 0) {
            snprintf(result, sizeof(result), "none");
        }
        break;
    }
    case RX_CALL:
        set(gui.address_str, MUIA_String_Contents, args[0]);
        ok = do_call();
        break;
    case RX_ANSWER: ok = do_answer(); break;
    case RX_HANGUP: ok = do_hangup(); break;
    case RX_PLAY: ok = do_play((const char *)args[0]); break;
    case RX_SNAPSHOT: ok = do_snapshot((const char *)args[0]); break;
    case RX_VIDEO: {
        int on = !gui.shown.video; /* without ON or OFF: switch */
        if (args[0] != 0) {
            on = 1;
        } else if (args[1] != 0) {
            on = 0;
        }
        ok = do_set_video(on);
        break;
    }
    case RX_SEND: {
        aos_text_to_utf8((const char *)args[0], a, sizeof(a));
        aos_text_to_utf8((const char *)args[1], b, sizeof(b));
        struct chat *chat = open_chat(a, TRUE);
        ok = chat != NULL && send_text(chat, b) == 0;
        break;
    }
    case RX_STATUS: {
        const struct gui_account *account = default_account();
        int n = snprintf(result, sizeof(result), "%s|port %d", account != NULL ? reg_name(account->state) : reg_name(-1),
                         gui.sip_port);
        for (int i = 0; i < AOS_CORE_MAX_CALLS && n > 0 && (size_t)n < sizeof(result); ++i) {
            if (gui.calls[i].id != 0) {
                aos_text_from_utf8(gui.calls[i].peer, c, sizeof(c));
                n += snprintf(result + n, sizeof(result) - (size_t)n, "|call %d %s %s%s", gui.calls[i].id,
                              call_state_name(gui.calls[i].state), c, gui.calls[i].video ? " video" : "");
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
    case MUIM_Phone_Unregister: {
        char identity[AOS_CORE_ADDR_MAX];
        chosen_identity(identity, sizeof(identity));
        return do_unregister(identity);
    }
    case MUIM_Phone_PickAccount: return do_pick_account();
    case MUIM_Phone_NewAccount: return do_new_account();
    case MUIM_Phone_RemoveAccount: return do_remove_account();
    case MUIM_Phone_UseAccount: return do_use_account();
    case MUIM_Phone_AccountForm: return do_account_form();
    case MUIM_Phone_CloseError: set(gui.error_win, MUIA_Window_Open, FALSE); return 0;
    case MUIM_Phone_Call: return do_call();
    case MUIM_Phone_Answer: return do_answer();
    case MUIM_Phone_Hangup: return do_hangup();
    case MUIM_Phone_OpenChat: return do_open_chat();
    case MUIM_Phone_SendChat: return do_send_chat((int)pmsg->arg1);
    case MUIM_Phone_ShowCall: return do_show_call();
    case MUIM_Phone_Rexx: return do_rexx((int)pmsg->arg1, (IPTR *)pmsg->arg2);
    case MUIM_Phone_Video: return do_video();
    case MUIM_Phone_SetVideo: return do_set_video((int)pmsg->arg1);
    case MUIM_Phone_Ring: return do_ring();
    }
    return DoSuperMethodA(cl, obj, msg);
BOOPSI_DISPATCHER_END
/* clang-format on */

/* ---- creation ---- */

static Object *create_windows(void) {
    Object *account_button, *call_button, *chat_button, *register_button, *unregister_button;
    Object *new_account_button, *remove_account_button, *use_account_button;

    /* clang-format off */
    gui.main_win = WindowObject,
        MUIA_Window_Title, (IPTR)"aos-phone",
        MUIA_Window_ID, MAKE_ID('A', 'P', 'M', 'W'),
        MUIA_Window_Width, MUIV_Window_Width_Screen(35),
        WindowContents, VGroup,
            Child, HGroup, GroupFrameT("Account"),
                Child, gui.account_lamp = NewObject(gui.lamp_class->mcc_Class, NULL,
                    AOS_LAMP_COLOUR, AOS_LAMP_RED, MUIA_ShortHelp, (IPTR)"no account",
                TAG_DONE),
                Child, gui.account_text = TextObject, TextFrame, MUIA_Background, MUII_TextBack, MUIA_HorizWeight, 300,
                    MUIA_Text_Contents, (IPTR)"no account",
                End,
                Child, account_button = SimpleButton("_Account..."),
            End,
            Child, HGroup, GroupFrameT("Address"),
                Child, gui.address_str = StringObject, StringFrame, MUIA_HorizWeight, 300,
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
            Child, VGroup, GroupFrameT("Accounts"),
                Child, ListviewObject,
                    MUIA_Listview_List, gui.accounts_list = ListObject, InputListFrame,
                        MUIA_List_ConstructHook, MUIV_List_ConstructHook_String,
                        MUIA_List_DestructHook, MUIV_List_DestructHook_String,
                    End,
                    MUIA_CycleChain, 1,
                End,
                Child, HGroup,
                    Child, new_account_button = SimpleButton("_New"),
                    Child, use_account_button = SimpleButton("Use for _calls"),
                    Child, remove_account_button = SimpleButton("Re_move..."),
                End,
            End,
            /* The fields hold the addresses without "sip:", which stands
               in front of them, and the identity gets its domain from the
               server. */
            Child, ColGroup(3),
                Child, Label2("Server:"),
                Child, Label2("sip:"),
                Child, gui.server_str = StringObject, StringFrame,
                    MUIA_String_MaxLen, AOS_CORE_ADDR_MAX, MUIA_CycleChain, 1,
                    MUIA_ShortHelp, (IPTR)"The SIP server of the account,\n"
                                          "e.g. sip.linphone.org or 10.8.0.1:5060.",
                End,
                Child, Label2("Identity:"),
                Child, Label2("sip:"),
                Child, HGroup,
                    Child, gui.identity_str = StringObject, StringFrame,
                        MUIA_String_MaxLen, AOS_CORE_ADDR_MAX, MUIA_CycleChain, 1,
                        MUIA_ShortHelp, (IPTR)"The user name of the account; the domain\n"
                                              "comes from the server, as shown on the right.\n"
                                              "An account in another domain: user@domain.",
                    End,
                    Child, gui.domain_text = TextObject,
                        MUIA_Frame, MUIV_Frame_String, MUIA_FramePhantomHoriz, TRUE,
                        MUIA_Text_SetMin, FALSE, MUIA_Text_Contents, (IPTR)"",
                        MUIA_ShortHelp, (IPTR)"The domain the identity gets from the server.",
                    End,
                End,
                Child, Label2("Password:"),
                Child, Label2(""),
                Child, gui.password_str = StringObject, StringFrame,
                    MUIA_String_MaxLen, 128, MUIA_String_Secret, TRUE, MUIA_CycleChain, 1,
                    MUIA_ShortHelp, (IPTR)"The password of the account. Left empty,\n"
                                          "an account registered again keeps the saved one.",
                End,
            End,
            Child, HGroup,
                Child, register_button = SimpleButton("_Register"),
                Child, unregister_button = SimpleButton("_Unregister"),
            End,
        End,
    End;

    /* No window ID: it always opens centred over the window it belongs to. */
    gui.error_win = WindowObject,
        MUIA_Window_Title, (IPTR)"aos-phone",
        WindowContents, VGroup,
            Child, gui.error_text = TextObject, TextFrame, MUIA_Background, MUII_TextBack,
                MUIA_Text_PreParse, (IPTR)"\33c", MUIA_Text_Contents, (IPTR)"",
            End,
            Child, HGroup,
                Child, HSpace(0),
                Child, gui.error_ok = SimpleButton("_OK"),
                Child, HSpace(0),
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
                Child, gui.ring_lamp = NewObject(gui.lamp_class->mcc_Class, NULL,
                    AOS_LAMP_WAVES, TRUE, AOS_LAMP_COLOUR, AOS_LAMP_GREEN, MUIA_ShowMe, FALSE,
                TAG_DONE),
                Child, gui.call_state_text = TextObject, MUIA_Text_Contents, (IPTR)"", End,
                Child, gui.call_time_text = TextObject, MUIA_Text_PreParse, (IPTR)"\33r",
                    MUIA_Text_Contents, (IPTR)"",
                End,
            End,
            Child, gui.video_group = HGroup, MUIA_ShowMe, FALSE,
                Child, gui.remote_video = aos_video_area_new(gui.video_class, gui.surface, AOS_VIDEO_REMOTE),
                Child, gui.preview_video = aos_video_area_new(gui.video_class, gui.surface, AOS_VIDEO_PREVIEW),
            End,
            Child, gui.mic_gauge = GaugeObject, GaugeFrame,
                MUIA_Gauge_Horiz, TRUE, MUIA_Gauge_Max, 100, MUIA_Gauge_InfoText, (IPTR)"Microphone",
            End,
            Child, gui.speaker_gauge = GaugeObject, GaugeFrame,
                MUIA_Gauge_Horiz, TRUE, MUIA_Gauge_Max, 100, MUIA_Gauge_InfoText, (IPTR)"Speaker",
            End,
            Child, HGroup,
                Child, gui.call_answer_button = SimpleButton("_Answer"),
                Child, gui.call_video_button = TextObject, ButtonFrame,
                    MUIA_Background, MUII_ButtonBack, MUIA_Font, MUIV_Font_Button,
                    MUIA_Text_PreParse, (IPTR)"\33c", MUIA_Text_Contents, (IPTR)"_Video",
                    MUIA_Text_HiCharIdx, '_', MUIA_ControlChar, 'v',
                    MUIA_InputMode, MUIV_InputMode_Toggle, MUIA_Disabled, TRUE,
                End,
                Child, gui.call_hangup_button = SimpleButton("_Hang up"),
            End,
        End,
    End;
    /* clang-format on */

    if (gui.main_win == NULL || gui.account_win == NULL || gui.error_win == NULL || gui.call_win == NULL) {
        gui.video_group = gui.remote_video = gui.preview_video = gui.account_lamp = gui.ring_lamp = gui.accounts_list =
            NULL;
        return NULL;
    }

    for (int i = 0; i < RX_COUNT; ++i) {
        gui.rexx_hooks[i].h_Entry = (HOOKFUNC)rexx_entry;
        gui.rexx_hooks[i].h_Data = (APTR)(IPTR)i;
    }

    /* clang-format off */
    Object *app = NewObject(gui.phone_class->mcc_Class, NULL,
        MUIA_Application_Title, (IPTR)"aos-phone",
        MUIA_Application_Version, (IPTR)"$VER: aos-phone " AOS_VERSION " (" AOS_VERSION_DATE ")",
        MUIA_Application_Description, (IPTR)"SIP phone",
        MUIA_Application_Base, (IPTR)"AOSPHONE",
        MUIA_Application_Commands, (IPTR)rexx_commands,
        SubWindow, (IPTR)gui.main_win,
        SubWindow, (IPTR)gui.account_win,
        SubWindow, (IPTR)gui.error_win,
        SubWindow, (IPTR)gui.call_win,
        TAG_DONE);
    /* clang-format on */
    if (app == NULL) {
        /* A Zune object that fails to come up disposes its children. */
        gui.main_win = gui.account_win = gui.error_win = gui.call_win = NULL;
        gui.video_group = gui.remote_video = gui.preview_video = gui.account_lamp = NULL;
        return NULL;
    }
    gui.app = app;

    DoMethod(gui.main_win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app, 2, MUIM_Application_ReturnID,
             MUIV_Application_ReturnID_Quit);
    DoMethod(account_button, MUIM_Notify, MUIA_Pressed, FALSE, gui.account_win, 3, MUIM_Set, MUIA_Window_Open, TRUE);
    DoMethod(gui.account_win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, MUIV_Notify_Self, 3, MUIM_Set,
             MUIA_Window_Open, FALSE);
    DoMethod(gui.error_ok, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_CloseError);
    DoMethod(gui.error_win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, app, 1, MUIM_Phone_CloseError);
    DoMethod(gui.server_str, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, app, 1, MUIM_Phone_AccountForm);
    DoMethod(gui.identity_str, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime, app, 1, MUIM_Phone_AccountForm);
    DoMethod(register_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Register);
    DoMethod(unregister_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Unregister);
    DoMethod(gui.accounts_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, app, 1, MUIM_Phone_PickAccount);
    DoMethod(new_account_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_NewAccount);
    DoMethod(use_account_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_UseAccount);
    DoMethod(remove_account_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_RemoveAccount);
    DoMethod(call_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Call);
    DoMethod(gui.address_str, MUIM_Notify, MUIA_String_Acknowledge, MUIV_EveryTime, app, 1, MUIM_Phone_Call);
    DoMethod(chat_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_OpenChat);
    DoMethod(gui.answer_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Answer);
    DoMethod(gui.call_answer_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Answer);
    DoMethod(gui.hangup_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Hangup);
    DoMethod(gui.call_hangup_button, MUIM_Notify, MUIA_Pressed, FALSE, app, 1, MUIM_Phone_Hangup);
    DoMethod(gui.call_video_button, MUIM_Notify, MUIA_Selected, MUIV_EveryTime, app, 2, MUIM_Phone_SetVideo,
             MUIV_TriggerValue);
    DoMethod(gui.calls_list, MUIM_Notify, MUIA_List_Active, MUIV_EveryTime, app, 1, MUIM_Phone_ShowCall);
    DoMethod(gui.calls_view, MUIM_Notify, MUIA_Listview_DoubleClick, TRUE, gui.call_win, 3, MUIM_Set,
             MUIA_Window_Open, TRUE);
    DoMethod(gui.call_win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE, MUIV_Notify_Self, 3, MUIM_Set,
             MUIA_Window_Open, FALSE);

    set(register_button, MUIA_ShortHelp, (IPTR) "Adds the account or updates it, and registers it.");
    set(unregister_button, MUIA_ShortHelp, (IPTR) "Stops registering the account; it stays saved.");

    Object *cycle[] = {account_button, call_button, chat_button, gui.answer_button, gui.hangup_button,
                       register_button, unregister_button, new_account_button, use_account_button,
                       remove_account_button, gui.error_ok, gui.call_answer_button, gui.call_video_button,
                       gui.call_hangup_button};
    for (size_t i = 0; i < sizeof(cycle) / sizeof(cycle[0]); ++i) {
        set(cycle[i], MUIA_CycleChain, 1);
    }
    return app;
}

static void video_notify(void *context) {
    (void)context;
    Signal(gui.task, 1UL << gui.video_signal);
}

int aos_gui_create(void) {
    gui.task = FindTask(NULL);
    gui.signal = AllocSignal(-1);
    gui.video_signal = AllocSignal(-1);
    if (gui.signal < 0 || gui.video_signal < 0) {
        aos_gui_dispose();
        return -1;
    }
    gui.phone_class = MUI_CreateCustomClass(NULL, (ClassID)MUIC_Application, NULL, sizeof(IPTR), phone_dispatcher);
    gui.video_class = aos_video_class_create();
    gui.lamp_class = aos_lamp_class_create();
    gui.surface = aos_video_surface_create(video_notify, NULL);
    if (gui.phone_class == NULL || gui.video_class == NULL || gui.lamp_class == NULL || gui.surface == NULL ||
        create_windows() == NULL) {
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
    gui.ring_ihn.ihn_Object = gui.app;
    gui.ring_ihn.ihn_Millis = RING_FRAME_MS;
    gui.ring_ihn.ihn_Flags = MUIIHNF_TIMER;
    gui.ring_ihn.ihn_Method = MUIM_Phone_Ring;
    gui.video_ihn.ihn_Object = gui.app;
    gui.video_ihn.ihn_Signals = 1UL << gui.video_signal;
    gui.video_ihn.ihn_Flags = 0;
    gui.video_ihn.ihn_Method = MUIM_Phone_Video;
    DoMethod(gui.app, MUIM_Application_AddInputHandler, (IPTR)&gui.events_ihn);
    DoMethod(gui.app, MUIM_Application_AddInputHandler, (IPTR)&gui.timer_ihn);
    DoMethod(gui.app, MUIM_Application_AddInputHandler, (IPTR)&gui.video_ihn);
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
            DoMethod(gui.app, MUIM_Application_RemInputHandler, (IPTR)&gui.video_ihn);
            DoMethod(gui.app, MUIM_Application_RemInputHandler, (IPTR)&gui.timer_ihn);
            if (gui.ring_running) {
                DoMethod(gui.app, MUIM_Application_RemInputHandler, (IPTR)&gui.ring_ihn);
                gui.ring_running = 0;
            }
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
        if (gui.error_win != NULL) {
            MUI_DisposeObject(gui.error_win);
        }
        if (gui.call_win != NULL) {
            MUI_DisposeObject(gui.call_win);
        }
    }
    gui.main_win = gui.account_win = gui.error_win = gui.call_win = NULL;
    gui.video_group = gui.remote_video = gui.preview_video = gui.account_lamp = gui.ring_lamp = gui.accounts_list =
        NULL;
    if (gui.phone_class != NULL) {
        MUI_DeleteCustomClass(gui.phone_class);
        gui.phone_class = NULL;
    }
    /* The objects of the class are gone with the windows. */
    aos_video_class_delete(gui.video_class);
    gui.video_class = NULL;
    aos_lamp_class_delete(gui.lamp_class);
    gui.lamp_class = NULL;
    aos_video_surface_destroy(gui.surface);
    gui.surface = NULL;
    if (gui.video_signal >= 0) {
        FreeSignal(gui.video_signal);
        gui.video_signal = -1;
    }
    if (gui.signal >= 0) {
        FreeSignal(gui.signal);
        gui.signal = -1;
    }
}

void *aos_gui_video_window(void) {
    return gui.surface;
}

void aos_gui_notify(void *context) {
    (void)context;
    Signal(gui.task, 1UL << gui.signal);
}
