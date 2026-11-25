/*
 * aos-phone - a SIP phone for AROS with a Zune interface, built on
 * liblinphone.
 *
 * usage: aos-phone [DATA=<directory>] [PORT=<number>] [RECORD=<directory>]
 *                  [CAMERA=<name>] [VERBOSE]
 *                  [TESTVIDEO=<file>] [TESTVIDEOSIZE=<w>x<h>] [TESTVIDEOFPS=<n>]
 *                  [TESTSOUND=<file>] [TESTSOUNDRATE=<n>]
 *
 *   DATA  where the linphone core keeps its configuration, including the
 *         account; default PROGDIR:aos-phone-data. It must be a directory,
 *         not the root of a volume.
 *   PORT  UDP port for SIP; default a random free port. A fixed port helps
 *         when others call this computer by its IP address.
 *   RECORD  records every call to a WAV file in this directory, both sides
 *         mixed; default no recording.
 *   CAMERA  sends video from the camera whose id contains this name, e.g.
 *         Mire (a moving test picture) or StaticImage; default the camera the
 *         linphone core chose.
 *   VERBOSE  prints all liblinphone messages, not only warnings and errors.
 *   TESTVIDEO  adds the camera "File: <file name>", which plays a file of
 *         raw RGB24 pictures in a loop, of TESTVIDEOSIZE pixels (default
 *         320x240) at TESTVIDEOFPS pictures per second (default 15). Calls
 *         send video from it unless CAMERA names another camera.
 *   TESTSOUND  makes the AHI card capture a file of raw 16-bit mono
 *         samples in the byte order of the processor, played in a loop at
 *         TESTSOUNDRATE samples per second (default 16000), so calls send
 *         it as the sound of the microphone, without echo cancellation.
 *
 * Started from its icon, the program takes the same settings from the tool
 * types of the same names. Its output then goes nowhere, unless the tool
 * type DEBUG asks for a console window.
 *
 * The SIP parsers read their grammars from PROGDIR:share/belr/grammars,
 * the ring tones come from PROGDIR:share/sounds/linphone, and the picture of
 * the StaticImage camera from PROGDIR:share/images.
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
#include "files/file_source.h"

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
#include <string.h>
#include <strings.h>

#define DEFAULT_DATA_DIR "PROGDIR:aos-phone-data"
#define RESOURCES_DIR    "PROGDIR:share"

#define DEFAULT_TEST_WIDTH  320
#define DEFAULT_TEST_HEIGHT 240
#define DEFAULT_TEST_FPS    15
#define DEFAULT_TEST_RATE   16000

/* The interface task: Zune and the C library need more than the default
   stack of a Shell command. */
ULONG __stack = 256 * 1024;

/* The console window the C library opens, on the first output, for a
   program started from Workbench. It stays open after the program ends, so
   the debug output can be read. */
char const __stdiowin[] = "CON:0/30/640/240/aos-phone debug output/AUTO/CLOSE/WAIT";

struct settings {
    char data_dir[256];
    char record_dir[256]; /* "": calls are not recorded */
    char camera[256];     /* "": the camera the core chose */
    int sip_port;
    int verbose;
    int debug;
    /* One byte more than a source takes, so that a path too long is
       reported as such instead of being cut. */
    char test_video[AOS_FILE_PATH_MAX + 1]; /* "": no file camera */
    char test_video_size[32];               /* "": the default size */
    int test_video_fps;
    char test_sound[AOS_FILE_PATH_MAX + 1]; /* "": the AHI card does not capture */
    int test_sound_rate;
};

/* The sources of the test files; the core thread uses them until it ends. */
static struct aos_file_video test_video;
static struct aos_file_audio test_sound;

/* Copies a setting, cut to the size of dst. */
static void copy_setting(char *dst, size_t size, const char *value) {
    size_t len = strlen(value) < size - 1 ? strlen(value) : size - 1;
    memcpy(dst, value, len);
    dst[len] = '\0';
}

static int read_arguments(struct settings *settings) {
    SIPTR args[10] = {0};
    struct RDArgs *rdargs = ReadArgs((CONST_STRPTR) "DATA/K,PORT/K/N,RECORD/K,CAMERA/K,VERBOSE/S,"
                                                    "TESTVIDEO/K,TESTVIDEOSIZE/K,TESTVIDEOFPS/K/N,"
                                                    "TESTSOUND/K,TESTSOUNDRATE/K/N",
                                     args, NULL);
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
    if (args[2] != 0) {
        snprintf(settings->record_dir, sizeof(settings->record_dir), "%s", (const char *)args[2]);
    }
    if (args[3] != 0) {
        snprintf(settings->camera, sizeof(settings->camera), "%s", (const char *)args[3]);
    }
    settings->verbose = args[4] != 0;
    if (args[5] != 0) {
        copy_setting(settings->test_video, sizeof(settings->test_video), (const char *)args[5]);
    }
    if (args[6] != 0) {
        copy_setting(settings->test_video_size, sizeof(settings->test_video_size), (const char *)args[6]);
    }
    if (args[7] != 0) {
        settings->test_video_fps = (int)*(LONG *)args[7];
    }
    if (args[8] != 0) {
        copy_setting(settings->test_sound, sizeof(settings->test_sound), (const char *)args[8]);
    }
    if (args[9] != 0) {
        settings->test_sound_rate = (int)*(LONG *)args[9];
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

/* The value of the tool type name: "" for a bare NAME, the text after '='
   for NAME=value, NULL when the icon has no such tool type. It compares the
   whole name, unlike FindToolType() of AROS, which also takes a tool type
   that only starts with the name: TESTVIDEO would find TESTVIDEOSIZE=... */
static const char *tool_type(const STRPTR *types, const char *name) {
    size_t len = strlen(name);
    for (; types != NULL && *types != NULL; ++types) {
        const char *type = (const char *)*types;
        if (strncasecmp(type, name, len) == 0 && (type[len] == '=' || type[len] == '\0')) {
            return type[len] == '=' ? type + len + 1 : type + len;
        }
    }
    return NULL;
}

/* Copies the text value of a tool type; an empty one is ignored. */
static void path_tool_type(const STRPTR *types, const char *name, char *dst, size_t size) {
    const char *value = tool_type(types, name);
    if (value != NULL && value[0] != '\0') {
        copy_setting(dst, size, value);
    }
}

/* Takes the settings and DEBUG from the tool types of the icon the program
   was started from. */
static void read_tool_types(struct settings *settings) {
    struct WBArg *arg = &WBenchMsg->sm_ArgList[0];
    BPTR old_dir = CurrentDir(arg->wa_Lock);
    struct DiskObject *icon = GetDiskObject((CONST_STRPTR)arg->wa_Name);
    CurrentDir(old_dir);
    if (icon == NULL) {
        return;
    }
    const STRPTR *types = (const STRPTR *)icon->do_ToolTypes;
    const char *value;
    path_tool_type(types, "DATA", settings->data_dir, sizeof(settings->data_dir));
    if ((value = tool_type(types, "PORT")) != NULL) {
        settings->sip_port = atoi(value);
    }
    path_tool_type(types, "RECORD", settings->record_dir, sizeof(settings->record_dir));
    path_tool_type(types, "CAMERA", settings->camera, sizeof(settings->camera));
    settings->verbose = tool_type(types, "VERBOSE") != NULL;
    settings->debug = tool_type(types, "DEBUG") != NULL;
    path_tool_type(types, "TESTVIDEO", settings->test_video, sizeof(settings->test_video));
    path_tool_type(types, "TESTVIDEOSIZE", settings->test_video_size, sizeof(settings->test_video_size));
    if ((value = tool_type(types, "TESTVIDEOFPS")) != NULL) {
        settings->test_video_fps = atoi(value);
    }
    path_tool_type(types, "TESTSOUND", settings->test_sound, sizeof(settings->test_sound));
    if ((value = tool_type(types, "TESTSOUNDRATE")) != NULL) {
        settings->test_sound_rate = atoi(value);
    }
    FreeDiskObject(icon);
}

/* Sets up the sources of TESTVIDEO and TESTSOUND. Returns NULL, or what is
   wrong, in message. */
static const char *setup_test_files(struct settings *settings, char *message, size_t size) {
    if (settings->test_video[0] != '\0') {
        int width = DEFAULT_TEST_WIDTH, height = DEFAULT_TEST_HEIGHT;
        char rest;
        if (settings->test_video_size[0] != '\0' &&
            sscanf(settings->test_video_size, "%dx%d%c", &width, &height, &rest) != 2) {
            return "TESTVIDEOSIZE must be <width>x<height>, e.g. 320x240";
        }
        const char *error = aos_file_video_init(&test_video, settings->test_video, width, height,
                                                (float)settings->test_video_fps);
        if (error != NULL) {
            snprintf(message, size, "TESTVIDEO %s: %s", settings->test_video, error);
            return message;
        }
        if (settings->camera[0] == '\0') {
            snprintf(settings->camera, sizeof(settings->camera), "%s: %s", test_video.source.driver,
                     test_video.source.name);
        }
    }
    if (settings->test_sound[0] != '\0') {
        const char *error = aos_file_audio_init(&test_sound, settings->test_sound, settings->test_sound_rate, 1);
        if (error != NULL) {
            snprintf(message, size, "TESTSOUND %s: %s", settings->test_sound, error);
            return message;
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    (void)argv;
    struct settings settings = {
        .data_dir = DEFAULT_DATA_DIR,
        .test_video_fps = DEFAULT_TEST_FPS,
        .test_sound_rate = DEFAULT_TEST_RATE,
    };
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

    char message[2 * AOS_FILE_PATH_MAX + 128];
    const char *test_error = setup_test_files(&settings, message, sizeof(message));
    if (test_error != NULL) {
        return fail(test_error);
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
    struct aos_core_config config = {
        .data_dir = settings.data_dir,
        .resources_dir = RESOURCES_DIR,
        .sip_port = settings.sip_port,
        .record_dir = settings.record_dir,
        .video_window = aos_gui_video_window(),
        .camera = settings.camera,
        .extra_camera = settings.test_video[0] != '\0' ? &test_video.source : NULL,
        .capture = settings.test_sound[0] != '\0' ? &test_sound.source : NULL,
        .capture_without_echo = settings.test_sound[0] != '\0',
        .verbose = settings.verbose,
        .notify = aos_gui_notify,
        .context = NULL,
    };
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
