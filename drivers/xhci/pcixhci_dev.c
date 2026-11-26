/* dev.c - pcixhci.device by Chris Hodges
 */

#include "debug.h"

#include <proto/exec.h>
#include <proto/oop.h>
#define __UTILITY_NOLIBBASE__
#include <proto/utility.h>

#include LC_LIBDEFS_FILE

#include "uhwcmd.h"

#define DEVNAME "pcixhci.device"

#define NewList NEWLIST

const char devname[] = MOD_NAME_STRING;
static const char strOopLibraryName[] = "oop.library";
static const char strUtilityLibraryName[] = "utility.library";

#if defined(__MORPHOS__)
/* no OOP attribute or method bases */
#elif defined(__OOP_NOATTRBASES__)
/* Keep order the same as order of IDs in struct e1000Base! */
static CONST_STRPTR const GM_UNIQUENAME(AttrBaseIDs)[] = {
    IID_Hidd,
    IID_Hidd_PCIDevice,
    NULL};
#else
OOP_AttrBase __IHidd;
OOP_AttrBase __IHidd_PCIDevice;
#endif

#if defined(__OOP_NOMETHODBASES__)
static CONST_STRPTR const GM_UNIQUENAME(MethBaseIDs)[] = {
    IID_Hidd_PCI,
    IID_Hidd_PCIDevice,
    IID_Hidd_PCIDriver,
    IID_HW,
    IID_Hidd,
    NULL};
#endif
#if defined(AROS_USE_LOGRES)
#ifdef LogResBase
#undef LogResBase
#endif
#ifdef LogResHandle
#undef LogResHandle
#endif
#define LogResBase base->hd_LogResBase
#define LogHandle  base->hd_LogRHandle
#endif
static int devInit(LIBBASETYPEPTR base) {
    KPRINTF(10, "base @ 0x%p, SysBase = 0x%p\n",
            base, SysBase);

#if defined(__OOP_NOLIBBASE__)
    if ((base->hd_OOPBase = OpenLibrary(strOopLibraryName, 0)) == NULL) {
        KPRINTF(10, "devInit: Failed to open oop.library!\n");
        return FALSE;
    }
#endif
#if defined(__OOP_NOATTRBASES__)
    if (OOP_ObtainAttrBasesArray(&base->hd_HiddAB, GM_UNIQUENAME(AttrBaseIDs))) {
        KPRINTF(10, "devInit: Failed to obtain OOP AttrBases!\n");
#if defined(__OOP_NOLIBBASE__)
        CloseLibrary(base->hd_OOPBase);
#endif
        return FALSE;
    }
#endif
#if defined(__OOP_NOMETHODBASES__)
    if (OOP_ObtainMethodBasesArray(&base->hd_HiddPCIMB, GM_UNIQUENAME(MethBaseIDs))) {
        KPRINTF(10, "devInit: Failed to obtain OOP MethodBases!\n");
#if defined(__OOP_NOATTRBASES__)
        OOP_ReleaseAttrBasesArray(&base->hd_HiddAB, GM_UNIQUENAME(AttrBaseIDs));
#endif
#if defined(__OOP_NOLIBBASE__)
        CloseLibrary(base->hd_OOPBase);
#endif
        return FALSE;
    }
#endif

    base->hd_UtilityBase = (struct UtilityBase *)OpenLibrary(strUtilityLibraryName, 39);

#define UtilityBase base->hd_UtilityBase

    if (UtilityBase) {
        base->hd_MemPool = CreatePool(MEMF_PUBLIC | MEMF_CLEAR | MEMF_SEM_PROTECTED,
                                      16384, 4096);
        if (base->hd_MemPool) {
            NewList(&base->hd_Units);

            KPRINTF(10, "Ok\n");
        } else {
            KPRINTF(10, "CreatePool() failed!\n");
            CloseLibrary((struct Library *)UtilityBase);
            base = NULL;
        }
    } else {
        KPRINTF(10, "OpenLibrary(\"utility.library\", 39) failed!\n");
        base = NULL;
    }

    KPRINTF(10, "openCnt = %ld\n", base->hd_Device.dd_Library.lib_OpenCnt);

    return base ? TRUE : FALSE;
}

/*
 *===========================================================
 * devOpen(ioreq, unit, flags, base)
 *===========================================================
 *
 * This is the the DEV_OPEN function.
 *
 */
static int devOpen(LIBBASETYPEPTR base, struct IOUsbHWReq *ioreq, ULONG unit, ULONG flags) {
    KPRINTF(10, "ioreq: 0x%p unit: %ld flags: 0x%08lx base: 0x%p\n",
            ioreq, unit, flags, base);

    KPRINTF(10, "openCnt = %ld\n", base->hd_Device.dd_Library.lib_OpenCnt);

    if (ioreq->iouh_Req.io_Message.mn_Length < sizeof(struct IOUsbHWReq)) {
        KPRINTF(20, "invalid MN_LENGTH!\n");

        ioreq->iouh_Req.io_Error = IOERR_BADLENGTH;
    } else {
        /* Default to open failure. */
        ioreq->iouh_Req.io_Error = IOERR_OPENFAIL;

        ioreq->iouh_Req.io_Unit = Open_Unit(ioreq, unit, base);
        if (!ioreq->iouh_Req.io_Unit) {
            KPRINTF(20, "could not open unit!\n");
        } else {
            /* Opended ok! */
            ioreq->iouh_Req.io_Message.mn_Node.ln_Type = NT_REPLYMSG;
            ioreq->iouh_Req.io_Error = 0;

            return TRUE;
        }
    }

    return FALSE;
}

/*
 *===========================================================
 * devClose(ioreq, base)
 *===========================================================
 *
 * This is the the DEV_EXPUNGE function.
 *
 */

static int devClose(LIBBASETYPEPTR base, struct IOUsbHWReq *ioreq) {
    KPRINTF(10, "ioreq: 0x%p base: 0x%p\n", ioreq, base);

    Close_Unit(base, (struct PCIUnit *)ioreq->iouh_Req.io_Unit, ioreq);

    ioreq->iouh_Req.io_Unit = (APTR)-1;
    ioreq->iouh_Req.io_Device = (APTR)-1;
    return TRUE;
}

static int devExpunge(LIBBASETYPEPTR base) {
    pciExpunge(base);

    DeletePool(base->hd_MemPool);

    KPRINTF(5, "closelibrary utilitybase 0x%p\n",
            UtilityBase);
    CloseLibrary((struct Library *)UtilityBase);
    return TRUE;
}

ADD2INITLIB(devInit, 0)
ADD2OPENDEV(devOpen, 0)
ADD2CLOSEDEV(devClose, 0)
ADD2EXPUNGELIB(devExpunge, 0)

/*
 *===========================================================
 * devBeginIO(ioreq, base)
 *===========================================================
 *
 * This is the DEV_BEGINIO vector of the device.
 *
 */
AROS_LH1(void, devBeginIO,
         AROS_LHA(struct IOUsbHWReq *, ioreq, A1),
         LIBBASETYPEPTR, base, 5, pcixhci) {
    AROS_LIBFUNC_INIT

    struct PCIUnit *unit = (struct PCIUnit *)ioreq->iouh_Req.io_Unit;
    WORD ret;

    // KPRINTF(1, ("devBeginIO ioreq: 0x%08lx base: 0x%08lx cmd: %lu\n", ioreq, base, ioreq->iouh_Req.io_Command));

    ioreq->iouh_Req.io_Message.mn_Node.ln_Type = NT_MESSAGE;
    ioreq->iouh_Req.io_Error = UHIOERR_NO_ERROR;

    if (ioreq->iouh_Req.io_Command < NSCMD_DEVICEQUERY) {
        switch (ioreq->iouh_Req.io_Command) {
        case CMD_RESET:
            ret = cmdReset(ioreq, unit, base);
            break;

        case CMD_FLUSH:
            ret = cmdFlush(ioreq, unit, base);
            break;

        case UHCMD_QUERYDEVICE:
            ret = cmdQueryDevice(ioreq, unit, base);
            break;

        case UHCMD_USBRESET:
            ret = cmdUsbReset(ioreq, unit, base);
            break;

        case UHCMD_USBRESUME:
            ret = cmdUsbResume(ioreq, unit, base);
            break;

        case UHCMD_USBSUSPEND:
            ret = cmdUsbSuspend(ioreq, unit, base);
            break;

        case UHCMD_USBOPER:
            ret = cmdUsbOper(ioreq, unit, base);
            break;

        case UHCMD_CONTROLXFER:
            ret = cmdControlXFer(ioreq, unit, base);
            break;

        case UHCMD_BULKXFER:
            ret = cmdBulkXFer(ioreq, unit, base);
            break;

        case UHCMD_INTXFER:
            ret = cmdIntXFer(ioreq, unit, base);
            break;

        case UHCMD_ISOXFER:
            ret = cmdIsoXFer(ioreq, unit, base);
            break;

#if defined(PCIUSB_WIP_ISO)
        case UHCMD_ADDISOHANDLER:
            ret = cmdAddIsoHandler(ioreq, unit, base);
            break;

        case UHCMD_REMISOHANDLER:
            ret = cmdRemIsoHandler(ioreq, unit, base);
            break;

        case UHCMD_STARTRTISO:
            ret = cmdStartRTIso(ioreq, unit, base);
            break;

        case UHCMD_STOPRTISO:
            ret = cmdStopRTIso(ioreq, unit, base);
            break;
#endif

        default:
            ret = IOERR_NOCMD;
            break;
        }
    } else {
        switch (ioreq->iouh_Req.io_Command) {
        case NSCMD_DEVICEQUERY:
            ret = cmdNSDeviceQuery((struct IOStdReq *)ioreq, unit, base);
            break;

        default:
            ret = IOERR_NOCMD;
            break;
        }
    }

    if (ret != RC_DONTREPLY) {
        if (ret != RC_OK) {
            KPRINTF(1, "TermIO <err %04x>\n", ret);
            /* Set error codes */
            ioreq->iouh_Req.io_Error = ret & 0xff;
        } else {
            KPRINTF(1, ("TermIO\n"));
        }
        /* Terminate the iorequest */
        TermIO(ioreq, base);
    }

    AROS_LIBFUNC_EXIT
}

/*
 *===========================================================
 * devAbortIO(ioreq, base)
 *===========================================================
 *
 * This is the DEV_ABORTIO vector of the device. It abort
 * the given iorequest, and set
 *
 */
AROS_LH1(LONG, devAbortIO,
         AROS_LHA(struct IOUsbHWReq *, ioreq, A1),
         LIBBASETYPEPTR, base, 6, pcixhci) {
    AROS_LIBFUNC_INIT

    KPRINTF(50, "devAbortIO ioreq: 0x%p, command %ld, status %ld\n", ioreq, ioreq->iouh_Req.io_Command,
            ioreq->iouh_Req.io_Message.mn_Node.ln_Type);

    /* Is it pending? */
    if (ioreq->iouh_Req.io_Message.mn_Node.ln_Type == NT_MESSAGE) {
        if (cmdAbortIO(ioreq, base)) {
            return (0);
        }
    }
    return (-1);

    AROS_LIBFUNC_EXIT
}

#if defined(__MORPHOS__)
/*
 * MorphOS device skeleton: what genmodule builds on AROS. The functions are
 * native PPC code; exec calls them with their arguments in the emulated
 * 68k registers (FUNCARRAY_32BIT_NATIVE).
 */
#include <exec/resident.h>

#undef UtilityBase
struct ExecBase *SysBase;
struct Library *PCIXBase;
struct Library *UtilityBase;

static const char devidstring[] = MOD_NAME_STRING " " VERSION_STRING " (" DATE_STRING ") MorphOS port of the AROS xHCI driver\r\n";
const char devverstag[] = "\0$VER: " MOD_NAME_STRING " " VERSION_STRING " (" DATE_STRING ")";

/* MorphOS loads this as a PPC module. */
const ULONG __amigappc__ = 1;

/* Run as a command, the file does nothing. */
int __start(void);
int __start(void) {
    return -1;
}

static BPTR devSegList;

static BPTR xhciMorphOSExpunge(struct PCIDevice *base) {
    BPTR seglist;
    if (base->hd_Device.dd_Library.lib_OpenCnt) {
        base->hd_Device.dd_Library.lib_Flags |= LIBF_DELEXP;
        return BNULL;
    }
    Remove(&base->hd_Device.dd_Library.lib_Node);
    devExpunge(base);
    CloseLibrary(PCIXBase);
    seglist = devSegList;
    FreeMem((UBYTE *)base - base->hd_Device.dd_Library.lib_NegSize,
            base->hd_Device.dd_Library.lib_NegSize + base->hd_Device.dd_Library.lib_PosSize);
    return seglist;
}

static struct PCIDevice *xhciMorphOSInit(struct PCIDevice *base, BPTR seglist, struct ExecBase *sysbase) {
    SysBase = sysbase;
    devSegList = seglist;
    base->hd_Device.dd_Library.lib_Node.ln_Type = NT_DEVICE;
    base->hd_Device.dd_Library.lib_Node.ln_Name = (char *)devname;
    base->hd_Device.dd_Library.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    base->hd_Device.dd_Library.lib_Version = VERSION_NUMBER;
    base->hd_Device.dd_Library.lib_Revision = REVISION_NUMBER;
    base->hd_Device.dd_Library.lib_IdString = (char *)devidstring;
    PCIXBase = OpenLibrary("pcix.library", 0);
    if (PCIXBase && devInit(base)) {
        UtilityBase = (struct Library *)base->hd_UtilityBase;
        return base;
    }
    if (PCIXBase)
        CloseLibrary(PCIXBase);
    FreeMem((UBYTE *)base - base->hd_Device.dd_Library.lib_NegSize,
            base->hd_Device.dd_Library.lib_NegSize + base->hd_Device.dd_Library.lib_PosSize);
    return NULL;
}

static void stub_devOpen(void) {
    struct IOUsbHWReq *ioreq = (struct IOUsbHWReq *)REG_A1;
    ULONG unit = (ULONG)REG_D0;
    ULONG flags = (ULONG)REG_D1;
    struct PCIDevice *base = (struct PCIDevice *)REG_A6;

    base->hd_Device.dd_Library.lib_OpenCnt++;
    if (devOpen(base, ioreq, unit, flags)) {
        base->hd_Device.dd_Library.lib_Flags &= ~LIBF_DELEXP;
    } else {
        base->hd_Device.dd_Library.lib_OpenCnt--;
    }
}

static BPTR stub_devClose(void) {
    struct IOUsbHWReq *ioreq = (struct IOUsbHWReq *)REG_A1;
    struct PCIDevice *base = (struct PCIDevice *)REG_A6;

    devClose(base, ioreq);
    if (--base->hd_Device.dd_Library.lib_OpenCnt == 0 && (base->hd_Device.dd_Library.lib_Flags & LIBF_DELEXP))
        return xhciMorphOSExpunge(base);
    return BNULL;
}

static BPTR stub_devExpunge(void) {
    return xhciMorphOSExpunge((struct PCIDevice *)REG_A6);
}

static ULONG stub_devReserved(void) {
    return 0;
}

static void stub_devBeginIO(void) {
    devBeginIO((struct IOUsbHWReq *)REG_A1, (struct PCIDevice *)REG_A6);
}

static LONG stub_devAbortIO(void) {
    return devAbortIO((struct IOUsbHWReq *)REG_A1, (struct PCIDevice *)REG_A6);
}

static const ULONG devFuncTable[] =
    {
        FUNCARRAY_32BIT_NATIVE,
        (ULONG)stub_devOpen,
        (ULONG)stub_devClose,
        (ULONG)stub_devExpunge,
        (ULONG)stub_devReserved,
        (ULONG)stub_devBeginIO,
        (ULONG)stub_devAbortIO,
        0xffffffff};

struct xhciMorphOSInitTable {
    ULONG size;
    const void *functable;
    const void *datatable;
    void (*initfunc)(void);
};

static const struct xhciMorphOSInitTable devInitTable =
    {
        sizeof(struct PCIDevice),
        devFuncTable,
        NULL,
        (void (*)(void))xhciMorphOSInit};

const struct Resident devROMTag =
    {
        .rt_MatchWord = RTC_MATCHWORD,
        .rt_MatchTag = (struct Resident *)&devROMTag,
        .rt_EndSkip = (struct Resident *)(&devROMTag + 1),
        .rt_Flags = RTF_PPC | RTF_AUTOINIT,
        .rt_Version = VERSION_NUMBER,
        .rt_Type = NT_DEVICE,
        .rt_Pri = 0,
        .rt_Name = (char *)devname,
        .rt_IdString = (char *)devidstring,
        .rt_Init = (APTR)&devInitTable,
        .rt_Revision = REVISION_NUMBER,
        .rt_Tags = NULL};
#endif /* __MORPHOS__ */
