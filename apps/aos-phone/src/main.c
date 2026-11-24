/*
 * aos-phone - a SIP phone for AROS with a Zune interface, built on
 * liblinphone.
 *
 * usage: aos-phone [DATA=<directory>] [PORT=<number>]
 *
 *   DATA  where the linphone core keeps its configuration, including the
 *         account; default PROGDIR:aos-phone-data. It must be a directory,
 *         not the root of a volume.
 *   PORT  UDP port for SIP; default a random free port. A fixed port helps
 *         when others call this computer by its IP address.
 *
 * Started from its icon, the program takes the same settings from the tool
 * types DATA and PORT. Its output then goes nowhere, unless the tool type
 * DEBUG asks for a console window.
 *
 * The SIP parsers read their grammars from PROGDIR:share/belr/grammars,
 * and the ring tones come from PROGDIR:share/sounds/linphone.
 * The program prints what happens to its standard output and offers the
 * ARexx port AOSPHONE.1; see README.md.
 *
 * Exit code: 0, or 20 when the program cannot start (no network, no
 * interface).
 */
#include "core.h"
#include "gui.h"
#include "text.h"

#include "aos/net.h"

#include <aros/startup.h>
#include <dos/dos.h>
#include <exec/types.h>
#include <workbench/startup.h>
#include <intuition/intuition.h>
#include <workbench/workbench.h>
#include <proto/dos.h>
#include <proto/icon.h>
#include <proto/intuition.h>

#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_DATA_DIR "PROGDIR:aos-phone-data"
#define RESOURCES_DIR    "PROGDIR:share"

/* The interface task: Zune and the C library need more than the default
   stack of a Shell command. */
ULONG __stack = 256 * 1024;

/* The console window the C library opens, on the first output, for a
   program started from Workbench. It stays open after the program ends, so
   the debug output can be read. */
char const __stdiowin[] = "CON:0/30/640/240/aos-phone debug output/AUTO/CLOSE/WAIT";

struct settings {
    char data_dir[256];
    int sip_port;
    int debug;
};

static int read_arguments(struct settings *settings) {
    SIPTR args[2] = {0, 0};
    struct RDArgs *rdargs = ReadArgs((CONST_STRPTR) "DATA/K,PORT/K/N", args, NULL);
    if (rdargs == NULL) {
        PrintFault(IoErr(), (CONST_STRPTR) "aos-phone");
        return -1;
    }
    if (args[0] != 0) {
        snprintf(settings->data_dir, sizeof(settings->data_dir), "%s", (const char *)args[0]);
    }
    if (args[1] != 0) {
        settings->sip_port = (int)*(LONG *)args[1];
    }
    FreeArgs(rdargs);
    return 0;
}

/* Reports a failure to start: on the standard error, and in a requester
   when started from Workbench, whose output is usually hidden. */
static int fail(const char *message) {
    fprintf(stderr, "aos-phone: %s\n", message);
    if (WBenchMsg != NULL) {
        struct EasyStruct request = {sizeof(struct EasyStruct), 0, (CONST_STRPTR) "aos-phone", (CONST_STRPTR) "%s",
                                     (CONST_STRPTR) "OK"};
        IPTR args[1] = {(IPTR)message};
        EasyRequestArgs(NULL, &request, NULL, (RAWARG)args);
    }
    return 20;
}

/* Takes DATA, PORT and DEBUG from the icon the program was started from. */
static void read_tool_types(struct settings *settings) {
    struct WBArg *arg = &WBenchMsg->sm_ArgList[0];
    BPTR old_dir = CurrentDir(arg->wa_Lock);
    struct DiskObject *icon = GetDiskObject((CONST_STRPTR)arg->wa_Name);
    CurrentDir(old_dir);
    if (icon == NULL) {
        return;
    }
    const STRPTR *types = (const STRPTR *)icon->do_ToolTypes;
    UBYTE *value;
    if ((value = FindToolType(types, (const STRPTR) "DATA")) != NULL && value[0] != '\0') {
        snprintf(settings->data_dir, sizeof(settings->data_dir), "%s", (const char *)value);
    }
    if ((value = FindToolType(types, (const STRPTR) "PORT")) != NULL) {
        settings->sip_port = atoi((const char *)value);
    }
    settings->debug = FindToolType(types, (const STRPTR) "DEBUG") != NULL;
    FreeDiskObject(icon);
}

int main(int argc, char **argv) {
    (void)argv;
    struct settings settings = {DEFAULT_DATA_DIR, 0, 0};
    if (argc > 0) {
        if (read_arguments(&settings) != 0) {
            return 20;
        }
    } else {
        read_tool_types(&settings);
        if (!settings.debug) {
            /* Nothing reaches the console, so it never opens. */
            freopen("NIL:", "w", stdout);
            freopen("NIL:", "w", stderr);
        }
    }

    if (aos_net_status() != 0) {
        return fail("no network: bsdsocket.library is not available");
    }
    if (aos_text_init() != 0) {
        fprintf(stderr, "aos-phone: no converter for the system code page; text beyond ASCII shows as '?'\n");
    }
    if (aos_gui_create() != 0) {
        aos_text_cleanup();
        return fail("cannot create the interface");
    }
    struct aos_core_config config = {settings.data_dir, RESOURCES_DIR, settings.sip_port, aos_gui_notify, NULL};
    if (aos_core_start(&config) != 0) {
        aos_gui_dispose();
        aos_text_cleanup();
        return fail("cannot start the core thread");
    }
    printf("aos-phone: running, data in %s\n", settings.data_dir);

    aos_gui_run();

    printf("aos-phone: stopping\n");
    aos_core_stop();
    aos_gui_dispose();
    aos_text_cleanup();
    return 0;
}
