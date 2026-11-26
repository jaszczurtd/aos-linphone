/*
 * bsdsocket.library for the whole program.
 *
 * The library is opened once, in the program's main task before main() and
 * before C++ static constructors run, and its base is shared with every
 * thread (SBTC_CAN_SHARE_LIBRARY_BASES). Without sharing, AROSTCP accepts a
 * base only from the task that opened it. Errors reach errno through
 * SBTC_ERROR_HOOK, which the stack calls in the context of the thread whose
 * call failed, so each thread sees its own errno.
 */
#include "net_internal.h"

#include "aos/net.h"

#include <aros/asmcall.h>
#include <aros/symbolsets.h>
#include <bsdsocket/socketbasetags.h>
#include <libraries/bsdsocket.h>
#include <proto/exec.h>
#include <utility/hooks.h>

#define AOS_NET_BSDSOCKET_VERSION 4

struct Library *SocketBase = NULL;
int aos_net_open_error = ENETDOWN;

/* AROS_USERFUNC_INIT and AROS_USERFUNC_EXIT expand to braces, which
   clang-format cannot see. */
/* clang-format off */
AROS_UFH3(static IPTR, aos_net_error_hook,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(APTR, object, A2),
    AROS_UFHA(struct ErrorHookMsg *, msg, A1)) {
    AROS_USERFUNC_INIT
    (void)hook;
    (void)object;
    if (msg->ehm_Action == EHMA_Set_errno) {
        errno = (int)msg->ehm_Code;
    }
    return 0;
    AROS_USERFUNC_EXIT
}

static struct Hook aos_net_hook = {
    {NULL, NULL}, (HOOKFUNC)aos_net_error_hook, NULL, NULL
};
/* clang-format on */

static int aos_net_open(void) {
    struct Library *base =
        OpenLibrary((CONST_STRPTR) "bsdsocket.library", AOS_NET_BSDSOCKET_VERSION);
    if (base == NULL) {
        /* AROSTCP is not running. The program still starts; every network
           call fails with ENETDOWN. */
        aos_net_open_error = ENETDOWN;
        return 1;
    }

    struct TagItem tags[] = {
        {SBTM_SETVAL(SBTC_ERROR_HOOK), (IPTR)&aos_net_hook},
        {SBTM_SETVAL(SBTC_CAN_SHARE_LIBRARY_BASES), TRUE},
        {TAG_DONE, 0},
    };
    /* Calls __SocketBaseTagList_WB() directly: SocketBase stays NULL until
       the base is fully set up. */
    if (__SocketBaseTagList_WB(base, tags) != 0) {
        CloseLibrary(base);
        aos_net_open_error = ENOSYS;
        return 1;
    }

    SocketBase = base;
    aos_net_open_error = 0;
    return 1;
}

static void aos_net_close(void) {
    if (SocketBase != NULL) {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        aos_net_open_error = ENETDOWN;
    }
}

ADD2INIT(aos_net_open, 10);
ADD2EXIT(aos_net_close, 10);

int aos_net_status(void) {
    return aos_net_open_error;
}

ULONG SocketBaseTagList(struct TagItem *tags) {
    if (SocketBase != NULL) {
        return __SocketBaseTagList_WB(SocketBase, tags);
    }

    /* Without the library, answer the errno and h_errno pointer queries with
       a zeroed variable, so that the h_errno helper in <netdb.h> does not
       read an unset pointer. Any other tag fails; the result is the number
       of the first failed tag, as with the library. */
    static LONG no_library_value;
    ULONG failed = 0;
    for (ULONG i = 0; tags != NULL && tags[i].ti_Tag != TAG_DONE; ++i) {
        Tag tag = tags[i].ti_Tag;
        if (tag == SBTM_GETREF(SBTC_HERRNOLONGPTR) || tag == SBTM_GETREF(SBTC_ERRNOLONGPTR)) {
            no_library_value = 0;
            *(LONG **)tags[i].ti_Data = &no_library_value;
        } else if (failed == 0) {
            failed = i + 1;
        }
    }
    return failed;
}
