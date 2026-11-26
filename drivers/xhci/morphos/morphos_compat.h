/*
 * MorphOS port of the AROS pcixhci.device: what the AROS headers, the AROS
 * function and interrupt macros and genmodule provide there. Included first
 * in every file of the driver (through the stand-in AROS headers next to
 * this one and the -include of the build).
 *
 * Copyright (C) 2026 Marcin "Jaszczur" Kielesiński.
 * Licensed under the AROS Public License version 1.1, like the driver.
 */
#ifndef XHCI_MORPHOS_COMPAT_H
#define XHCI_MORPHOS_COMPAT_H

#define USE_INLINE_STDARG
/* Poseidon calls go through a local library base named ps, as on AROS. */
#define POSEIDON_BASE_NAME ps

#include <exec/types.h>
#include <exec/lists.h>
#include <exec/nodes.h>
#include <exec/interrupts.h>
#include <emul/emulinterface.h>
#include <emul/emulregs.h>

/* Byte order: MorphOS runs big-endian, xHCI is little-endian. */
#define AROS_BIG_ENDIAN 1
#define AROS_WORD2LE(x) ((UWORD)__builtin_bswap16((UWORD)(x)))
#define AROS_LE2WORD(x) ((UWORD)__builtin_bswap16((UWORD)(x)))
#define AROS_LONG2LE(x) ((ULONG)__builtin_bswap32((ULONG)(x)))
#define AROS_LE2LONG(x) ((ULONG)__builtin_bswap32((ULONG)(x)))
#define AROS_QUAD2LE(x) ((UQUAD)__builtin_bswap64((UQUAD)(x)))
#define AROS_LE2QUAD(x) ((UQUAD)__builtin_bswap64((UQUAD)(x)))
#define AROS_WORD2BE(x) ((UWORD)(x))
#define AROS_LONG2BE(x) ((ULONG)(x))
#define AROS_BE2LONG(x) ((ULONG)(x))

#ifndef UQUAD_TYPEDEF
typedef unsigned long long UQUAD;
#endif

typedef void (*VOID_FUNC)(void);

#ifndef __packed
#define __packed __attribute__((__packed__))
#endif

/* Memory: MorphOS has no 31-bit flag; all its RAM is below 4 GB. */
#ifndef MEMF_31BIT
#define MEMF_31BIT 0
#endif

#define BNULL ((BPTR)0)

/* Lists */
#ifndef NEWLIST
#define NEWLIST(l)                                       \
    do {                                                 \
        (l)->lh_Head = (struct Node *)&(l)->lh_Tail;     \
        (l)->lh_Tail = NULL;                             \
        (l)->lh_TailPred = (struct Node *)&(l)->lh_Head; \
    } while (0)
#endif
#define NewMinList(l) NEWLIST((struct List *)(l))
#define ForeachNode(list, node)                                                               \
    for (node = (void *)(((struct List *)(list))->lh_Head); ((struct Node *)(node))->ln_Succ; \
         node = (void *)(((struct Node *)(node))->ln_Succ))
#define ForeachNodeSafe(list, node, next)                   \
    for (node = (void *)(((struct List *)(list))->lh_Head); \
         (next = (void *)((struct Node *)(node))->ln_Succ) != NULL; node = (void *)(next))

/*
 * Interrupt code (Cause(), interrupt servers): a MorphOS gate whose native
 * function gets is_Data from A1 and returns its result in D0. The gate is a
 * one-element array, so its name stands for its address, as the function's
 * name does on AROS.
 */
#define AROS_INTH1(name, type, var)                                                   \
    ULONG name##_impl(type var);                                                      \
    static ULONG name##_gate(void) {                                                  \
        return name##_impl((type)REG_A1);                                             \
    }                                                                                 \
    const struct EmulLibEntry name[1] = {{TRAP_LIB, 0, (void (*)(void))name##_gate}}; \
    ULONG name##_impl(type var)
#define AROS_INTP(name) extern const struct EmulLibEntry name[1]
#define AROS_INTFUNC_INIT
#define AROS_INTFUNC_EXIT

/* Task entry points: psdSpawnSubTask() starts 68k code, so the name stands
 * for a gate to the native function, as for interrupt code. */
#define AROS_UFH0(rt, name)                                                             \
    rt name##_impl(void);                                                               \
    const struct EmulLibEntry name[1] = {{TRAP_LIBNR, 0, (void (*)(void))name##_impl}}; \
    rt name##_impl(void)
#define AROS_UFP0(rt, name) extern const struct EmulLibEntry name[1]
#define AROS_USERFUNC_INIT
#define AROS_USERFUNC_EXIT

/* AROS's exec reset callbacks do not exist on MorphOS. */
#define AddResetCallback(i) ((void)0)
#define RemResetCallback(i) ((void)0)

/* Library functions: plain C functions; the MorphOS device skeleton in
 * pcixhci_dev.c calls them from stubs that read the 68k registers. */
#define AROS_LHA(type, var, reg)                    type var
#define AROS_LH1(rt, name, a1, bt, bn, offset, lib) rt name(a1, bt bn)
#define AROS_LIBFUNC_INIT
#define AROS_LIBFUNC_EXIT

/* Module set-up macros of genmodule; the MorphOS device skeleton calls the
 * functions itself. */
#define ADD2INITLIB(f, p)
#define ADD2EXPUNGELIB(f, p)
#define ADD2OPENDEV(f, p)
#define ADD2CLOSEDEV(f, p)

/* Topology of a request: see XHCI_IOREQ_ROOTPORT in pcixhci.h */
struct IOUsbHWReq;
UWORD xhciMorphOSRootPort(const struct IOUsbHWReq *ioreq);
#define XHCI_IOREQ_ROOTPORT(ioreq)    xhciMorphOSRootPort(ioreq)
#define XHCI_IOREQ_ROUTESTRING(ioreq) ((ULONG)0)

/* MorphOS's devices/usb_hub.h defines the UPSF_ port status flags as the
 * little-endian USB word read big-endian (UPSF_PORT_POWER is 0x0001), so
 * port status words go to the hub class without a swap. */
#define XHCI_HUBWORD2LE(x) ((UWORD)(x))

/* USB 3 additions of AROS's USB headers */
#define UHFB_SUPERSPEED       15
#define UHFF_SUPERSPEED       (1 << UHFB_SUPERSPEED)
#define UPSF_PORT_SUPER_SPEED 0x0080 /* 0x8000, swapped like the other UPSF_ flags */
#define UDT_SSHUB             0x2a
struct UsbSSHubDesc {
    UBYTE bLength;
    UBYTE bDescriptorType;
    UBYTE bNbrPorts;
    UWORD wHubCharacteristics;
    UBYTE bPwrOn2PwrGood;
    UBYTE bHubContrCurrent;
    UBYTE bHubHdrDecLat;
    UWORD wHubDelay;
    UWORD DeviceRemovable;
} __attribute__((__packed__));

#define UHFB_HUB      10
#define UHFF_HUB      (1 << UHFB_HUB)
#define UHFB_TT_MULTI 11
#define UHFF_TT_MULTI (1 << UHFB_TT_MULTI)
#define UHCB_USB30    31
#define UHCF_USB30    (1UL << UHCB_USB30)
/* QueryDevice tags of AROS's Poseidon for endpoint hooks (never asked on MorphOS) */
#define UHA_PrepareEndpoint    (UHA_Dummy + 0x22)
#define UHA_DestroyEndpoint    (UHA_Dummy + 0x23)
#define UDT_SUPERSPEED_EP_COMP 0x30
struct UsbSSEndpointCompDesc {
    UBYTE bLength;
    UBYTE bDescriptorType;
    UBYTE bMaxBurst;
    UBYTE bmAttributes;
    UWORD wBytesPerInterval;
};
/* SuperSpeed fields of AROS's struct IOUsbHWReq; MorphOS has no USB 3 */
#define XHCI_IOREQ_SS_MAXBURST(ioreq)         ((UBYTE)0)
#define XHCI_IOREQ_SS_MULT(ioreq)             ((UBYTE)0)
#define XHCI_IOREQ_SS_BYTESPERINTERVAL(ioreq) ((UWORD)0)

/* AROS's data cache invalidation flag; MorphOS clears (flushes) instead */
#define CACRF_InvalidateD CACRF_ClearD

/* No OOP on MorphOS: the PCI "device object" is the pcix.library board. */
typedef void OOP_Object;
typedef void OOP_Class;

/* pcix.library, opened by the device's init function */
extern struct Library *PCIXBase;

/* Diagnostics: kprintf of libdebug, to the serial port. */
void kprintf(const char *fmt, ...);
#define KPrintF kprintf
#define bug     kprintf

#endif /* XHCI_MORPHOS_COMPAT_H */
