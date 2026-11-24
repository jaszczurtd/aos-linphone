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
 * The SIP parsers read their grammars from PROGDIR:share/belr/grammars.
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

#include <dos/dos.h>
#include <exec/types.h>
#include <proto/dos.h>

#include <stdio.h>

#define DEFAULT_DATA_DIR "PROGDIR:aos-phone-data"
#define RESOURCES_DIR    "PROGDIR:share"

/* The interface task: Zune and the C library need more than the default
   stack of a Shell command. */
ULONG __stack = 256 * 1024;

int main(int argc, char **argv) {
    (void)argv;
    char data_dir[256];
    int sip_port = 0;
    snprintf(data_dir, sizeof(data_dir), "%s", DEFAULT_DATA_DIR);
    if (argc > 0) {
        SIPTR args[2] = {0, 0};
        struct RDArgs *rdargs = ReadArgs((CONST_STRPTR) "DATA/K,PORT/K/N", args, NULL);
        if (rdargs == NULL) {
            PrintFault(IoErr(), (CONST_STRPTR) "aos-phone");
            return 20;
        }
        if (args[0] != 0) {
            snprintf(data_dir, sizeof(data_dir), "%s", (const char *)args[0]);
        }
        if (args[1] != 0) {
            sip_port = (int)*(LONG *)args[1];
        }
        FreeArgs(rdargs);
    }

    if (aos_net_status() != 0) {
        fprintf(stderr, "aos-phone: no network: bsdsocket.library is not available\n");
        return 20;
    }
    if (aos_text_init() != 0) {
        fprintf(stderr, "aos-phone: no converter for the system code page; text beyond ASCII shows as '?'\n");
    }
    if (aos_gui_create() != 0) {
        fprintf(stderr, "aos-phone: cannot create the interface\n");
        aos_text_cleanup();
        return 20;
    }
    struct aos_core_config config = {data_dir, RESOURCES_DIR, sip_port, aos_gui_notify, NULL};
    if (aos_core_start(&config) != 0) {
        fprintf(stderr, "aos-phone: cannot start the core thread\n");
        aos_gui_dispose();
        aos_text_cleanup();
        return 20;
    }
    printf("aos-phone: running, data in %s\n", data_dir);

    aos_gui_run();

    printf("aos-phone: stopping\n");
    aos_core_stop();
    aos_gui_dispose();
    aos_text_cleanup();
    return 0;
}
