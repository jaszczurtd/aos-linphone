/*
 * MorphOS port of the AROS uvc.class: what the AROS module skeleton, the
 * AROS headers and genmodule provide there.
 *
 * Copyright (C) 2026, The AROS Development Team. All rights reserved.
 * MorphOS port: Copyright (C) 2026 Marcin "Jaszczur" Kielesiński.
 * Licensed under the AROS Public License version 1.1.
 */
#ifndef UVC_MORPHOS_H
#define UVC_MORPHOS_H

#define USE_INLINE_STDARG
#define POSEIDON_BASE_NAME ps

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/memory.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/semaphores.h>
#include <exec/errors.h>
#include <exec/execbase.h>
#include <emul/emulinterface.h>
#include <emul/emulregs.h>

#include <utility/utility.h>
#include <utility/tagitem.h>
#include <utility/hooks.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <devices/timer.h>

#include <devices/usb.h>
#include <devices/usbhardware.h>
#include <devices/usb_video.h>
#include <libraries/poseidon.h>
#include <libraries/usbclass.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/utility.h>
#include <proto/poseidon.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* VideoStreaming control selectors, missing from the MorphOS header. */
#ifndef UVVSCS_PROBE_CONTROL
#define UVVSCS_PROBE_CONTROL 0x01
#endif
#ifndef UVVSCS_COMMIT_CONTROL
#define UVVSCS_COMMIT_CONTROL 0x02
#endif

/* Module identity (genmodule's names on AROS). */
#define MOD_NAME_STRING "uvc.class"
#define VERSION_NUMBER  1
#define REVISION_NUMBER 0
#define MOD_VERS_STRING "1.0"
#define MOD_DATE_STRING "26.11.2026"

#define BNULL           ((BPTR)0)
#define AROS_BE2LONG(x) (x) /* MorphOS is big-endian */
#define AROS_LONG2BE(x) (x)

#ifndef NEWLIST
#define NEWLIST(l)                                       \
    do {                                                 \
        (l)->lh_Head = (struct Node *)&(l)->lh_Tail;     \
        (l)->lh_Tail = NULL;                             \
        (l)->lh_TailPred = (struct Node *)&(l)->lh_Head; \
    } while (0)
#endif
#undef NewList
#define NewList(l) NEWLIST(l)

#define ForeachNode(list, node)                                                               \
    for (node = (void *)(((struct List *)(list))->lh_Head); ((struct Node *)(node))->ln_Succ; \
         node = (void *)(((struct Node *)(node))->ln_Succ))

/* Library bases of the class, opened in its init function. */
extern struct ExecBase *SysBase;
extern struct DosLibrary *DOSBase;
extern struct Library *UtilityBase;

/* Messages go to Poseidon's log (Trident, or an errorlog listing); the class
 * runs without a console. Tasks only, never from the iso hooks. */
void uvc_log(ULONG level, const char *fmt, ...);
#define UVCLOG_INFO  RETURN_OK
#define UVCLOG_WARN  RETURN_WARN
#define UVCLOG_ERROR RETURN_ERROR

/* No debug channel in this port. */
#define KPRINTF(l, x) ((void)0)
#define bug(...)      ((void)0)

#define min(x, y) (((x) < (y)) ? (x) : (y))
#define max(x, y) (((x) > (y)) ? (x) : (y))

#endif /* UVC_MORPHOS_H */
