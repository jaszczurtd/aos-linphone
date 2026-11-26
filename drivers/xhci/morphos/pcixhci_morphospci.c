/*
    PCI access of pcixhci.device on MorphOS, through pcix.library: what
    pcixhci_arospci.c does with AROS's PCI hidd.

    Copyright (C) 2023-2026, The AROS Development Team. All rights reserved.
    MorphOS port: Copyright (C) 2026 Marcin "Jaszczur" Kielesiński.
    Licensed under the AROS Public License version 1.1, like the driver.
*/

#include <proto/exec.h>
#include <proto/utility.h>
#include <exec/machine.h>
#include <libraries/pcix.h>

#include <stdio.h>
#include <string.h>

#include "uhwcmd.h"
#include "xhciproto.h"

#define NewList NEWLIST

#ifdef base
#undef base
#endif
#define base hd

/* xHCI: class serial bus (0x0c), subclass USB (0x03), interface 0x30 */
#define PCI_CLASS_SERIAL_USB_FULL 0x0c03
#define PCI_INTERFACE_XHCI        0x30

static const char strPcixhciDeviceFormat[] = "pcixhci.device/%u";
static const char strProductPciPrefix[] = "PCI ";
static const char strProductXhci[] = "xHCI ";
static const char strProductUsb[] = " USB ";
static const char strProductHostController[] = " Host Controller";
static const char strEmpty[] = "";

BOOL xhciMorphOSControllerInit(struct PCIController *hc);

/* A controller found on the bus: the same set-up as pciEnumerator() of AROS. */
static void pciAddController(struct PCIDevice *hd, APTR board) {
    struct PCIController *hc;
    ULONG bus = PCIXGetBoardAttr(board, PCIXTAG_BUS);
    ULONG dev = PCIXGetBoardAttr(board, PCIXTAG_DEV);
    ULONG sub = PCIXGetBoardAttr(board, PCIXTAG_FUNCTION);

    hc = AllocPooled(hd->hd_MemPool, sizeof(struct PCIController));
    if (!hc)
        return;
    hc->hc_IsoPTDCount = PCIUSB_ISO_PTD_COUNT;
    hc->hc_Device = hd;
    hc->hc_DevID = (bus << 16) | (dev << 8) | sub;
    hc->hc_FunctionNum = sub;
    hc->hc_PCIDeviceObject = board;
    hc->hc_PCIDriverObject = NULL;
    hc->hc_PCIIntLine = PCIXReadConfigByte(board, 0x3c);
    hc->hc_PCIMemIsExec = FALSE;
    hc->hc_Quirks = 0;
    hc->hc_VendID = (UWORD)PCIXReadConfigWord(board, PCIXCONFIG_VENDOR);
    hc->hc_ProdID = (UWORD)PCIXReadConfigWord(board, PCIXCONFIG_DEVICE);

    NewList(&hc->hc_PeriodicTDQueue);
    NewList(&hc->hc_CtrlXFerQueue);
    NewList(&hc->hc_IntXFerQueue);
    NewList(&hc->hc_IsoXFerQueue);
    NewList(&hc->hc_BulkXFerQueue);
    NewList(&hc->hc_TDQueue);
    NewList(&hc->hc_AbortQueue);
    NewMinList(&hc->hc_RTIsoHandlers);

    kprintf("pcixhci: xHCI %04lx:%04lx at %lu:%lu.%lu, registers at 0x%08lx, interrupt line %lu\n",
            (ULONG)hc->hc_VendID, (ULONG)hc->hc_ProdID, bus, dev, sub,
            PCIXGetBoardAttr(board, PCIXTAG_BASEADDRESS0), (ULONG)hc->hc_PCIIntLine);
    AddTail(&hd->hd_TempHCIList, &hc->hc_Node);
}

/* /// "pciInit()" */
BOOL pciInit(struct PCIDevice *hd) {
    struct PCIController *hc;
    struct PCIController *nexthc;
    struct PCIUnit *hu;
    ULONG unitno = 0;
    APTR board = NULL;

    NewList(&hd->hd_TempHCIList);

    while ((board = PCIXFindBoardTags(board, PCIXFINDTAG_FULLCLASS, PCI_CLASS_SERIAL_USB_FULL, TAG_END))) {
        if (PCIXReadConfigByte(board, PCIXCONFIG_PROGINTERFACE) == PCI_INTERFACE_XHCI)
            pciAddController(hd, board);
    }

    // Create units with a list of host controllers having the same bus and device number.
    while (hd->hd_TempHCIList.lh_Head->ln_Succ) {
        int cnt;

        hu = AllocPooled(hd->hd_MemPool, sizeof(struct PCIUnit));
        if (!hu)
            return FALSE;
        hu->hu_Device = hd;
        hu->hu_UnitNo = unitno;

        NewList(&hu->hu_Controllers);
        NewList(&hu->hu_RHIOQueue);
        NewMinList(&hu->hu_FreeRTIsoNodes);
        for (cnt = 0; cnt < MAX_ROOT_PORTS; cnt++) {
            hu->hu_RTIsoNodes[cnt].rtn_PTDs = NULL;
            hu->hu_RTIsoNodes[cnt].rtn_PTDCount = 0;
            AddTail((struct List *)&hu->hu_FreeRTIsoNodes, (struct Node *)&hu->hu_RTIsoNodes[cnt].rtn_Node);
        }

        hu->hu_DevID = (ULONG)-1;
        ForeachNodeSafe(&hd->hd_TempHCIList, hc, nexthc) {
            if (hu->hu_DevID == (ULONG)-1)
                hu->hu_DevID = hc->hc_DevID;

            if (hc->hc_DevID == hu->hu_DevID) {
                Remove(&hc->hc_Node);
                hc->hc_Node.ln_Name = AllocVec(32, MEMF_CLEAR);
                if (hc->hc_Node.ln_Name)
                    sprintf(hc->hc_Node.ln_Name, strPcixhciDeviceFormat, (unsigned)(hu->hu_UnitNo & ~PCIUSBUNIT_MASK));
                hc->hc_Node.ln_Pri = HCITYPE_XHCI;
                hc->hc_Unit = hu;
                Enqueue(&hu->hu_Controllers, &hc->hc_Node);
            }
        }
        AddTail(&hd->hd_Units, (struct Node *)hu);
        unitno++;
    }
    return TRUE;
}
/* \\\ */

/* /// "pciStrcat()" */
STRPTR pciStrcat(STRPTR d, STRPTR s) {
    while (*d)
        d++;
    while ((*d++ = *s++))
        ;
    return --d;
}
/* \\\ */

static void pciusbUpdateVersion(UBYTE major, UBYTE minor, UBYTE *bestMajor, UBYTE *bestMinor) {
    if ((major > *bestMajor) || ((major == *bestMajor) && (minor > *bestMinor))) {
        *bestMajor = major;
        *bestMinor = minor;
    }
}

static void pciusbAppendVersion(STRPTR dest, UBYTE major, UBYTE minor) {
    STRPTR vers = pciStrcat(dest, (char *)strEmpty);
    vers[0] = (char)('0' + (major % 10));
    vers[1] = '.';
    vers[2] = (char)('0' + (minor % 10));
    vers[3] = 0;
}

/* /// "pciAllocUnit()" */
BOOL pciAllocUnit(struct PCIUnit *hu) {
    struct PCIController *hc;

    BOOL allocgood = TRUE;
    ULONG xhciports = 0;
    ULONG cnt;

    ULONG xhcicnt = 0;
    UBYTE hciMajor = 0;
    UBYTE hciMinor = 0;
    UBYTE usbMajor = 0;
    UBYTE usbMinor = 0;
    STRPTR prodname;

    ForeachNode(&hu->hu_Controllers, hc) {
        if (PCIXAttemptBoard(hc->hc_PCIDeviceObject))
            hc->hc_Flags |= HCF_ALLOCATED;
        else {
            kprintf("pcixhci: PCI board already in use\n");
            allocgood = FALSE;
        }
    }

    if (allocgood) {
        ForeachNode(&hu->hu_Controllers, hc) {
            /* The hardware class does this on AROS when it is instantiated. */
            allocgood = xhciMorphOSControllerInit(hc);
            if (allocgood)
                allocgood = xhciInit(hc, hu, hu->hu_TimerReq);
            if (allocgood) {
                xhcicnt++;
                xhciports += hc->hc_NumPorts;
                pciusbUpdateVersion(hc->hc_HCIVersionMajor, hc->hc_HCIVersionMinor, &hciMajor, &hciMinor);
                pciusbUpdateVersion(hc->hc_USBVersionMajor, hc->hc_USBVersionMinor, &usbMajor, &usbMinor);
            } else {
                kprintf("pcixhci: controller set-up failed\n");
                break;
            }
        }
    }

    if (!allocgood) {
        ForeachNode(&hu->hu_Controllers, hc) {
            if (hc->hc_Flags & HCF_ALLOCATED) {
                hc->hc_Flags &= ~HCF_ALLOCATED;
                PCIXReleaseBoard(hc->hc_PCIDeviceObject);
            }
        }
        return FALSE;
    }

    ULONG portbase = 0;
    ForeachNode(&hu->hu_Controllers, hc) {
        for (cnt = portbase; cnt < portbase + hc->hc_NumPorts; cnt++) {
            hu->hu_PortMapX[cnt] = hc;
            hc->hc_PortNum[cnt - portbase] = cnt;
        }
        portbase += hc->hc_NumPorts;
    }

    hu->hu_RootHubXPorts = xhciports;
    hu->hu_RootHubPorts = xhciports;
    hu->hu_FrameCounter = 1;
    hu->hu_RootHubAddr = 0;
    hu->hu_ResetPort = 0;

    // create product name of device
    prodname = (STRPTR)hu->hu_ProductName;
    *prodname = 0;
    pciStrcat(prodname, (char *)strProductPciPrefix);
    pciStrcat(prodname, (char *)strProductXhci);
    pciusbAppendVersion(prodname, hciMajor, hciMinor);

    // put em online
    ForeachNode(&hu->hu_Controllers, hc) {
        hc->hc_Flags |= HCF_ONLINE;
    }

    pciStrcat(prodname, (char *)strProductUsb);
    pciusbAppendVersion(prodname, usbMajor, usbMinor);
    pciStrcat(prodname, (char *)strProductHostController);
    kprintf("pcixhci: unit %ld online: %s, %lu ports\n", (LONG)(hu->hu_UnitNo & ~PCIUSBUNIT_MASK), prodname,
            xhciports);
    return TRUE;
}
/* \\\ */

/* /// "pciFreeUnit()" */
void pciFreeUnit(struct PCIUnit *hu) {
    struct PCIController *hc;

    // put em offline
    ForeachNode(&hu->hu_Controllers, hc) {
        hc->hc_Flags &= ~HCF_ONLINE;
        xhciFree(hc, hu);
    }

    ForeachNode(&hu->hu_Controllers, hc) {
        pciFreeAligned(hc, &hc->hc_PCIMem);
    }

    // disable and free board
    ForeachNode(&hu->hu_Controllers, hc) {
        /* deactivate bus mastering and memory */
        PCIXWriteConfigWord(hc->hc_PCIDeviceObject, PCIXCONFIG_COMMAND,
                            PCIXReadConfigWord(hc->hc_PCIDeviceObject, PCIXCONFIG_COMMAND) & ~0x0006);
        xhciMorphOSRemInterrupt(hc);
        if (hc->hc_Flags & HCF_ALLOCATED) {
            hc->hc_Flags &= ~HCF_ALLOCATED;
            PCIXReleaseBoard(hc->hc_PCIDeviceObject);
        }
    }
}
/* \\\ */

/* /// "pciExpunge()" */
void pciExpunge(struct PCIDevice *hd) {
    struct PCIController *hc, *ct;
    struct PCIUnit *hu, *ut;

    ForeachNodeSafe(&hd->hd_Units, hu, ut) {
        Remove((struct Node *)hu);
        ForeachNodeSafe(&hu->hu_Controllers, hc, ct) {
            Remove(&hc->hc_Node);
            FreeVec(hc->hc_Node.ln_Name);
            FreePooled(hd->hd_MemPool, hc, sizeof(struct PCIController));
        }
        FreePooled(hd->hd_MemPool, hu, sizeof(struct PCIUnit));
    }
}
/* \\\ */

/*
 * Root port (1-based) of the device a request goes to, for
 * XHCI_IOREQ_ROOTPORT(): MorphOS's struct IOUsbHWReq does not carry it. A
 * request to the default address 0, other than to the root hub itself before
 * it has an address, goes to the device on the port the hub class reset last.
 */
UWORD xhciMorphOSRootPort(const struct IOUsbHWReq *ioreq) {
    const struct PCIUnit *unit = (const struct PCIUnit *)ioreq->iouh_Req.io_Unit;

    if (!unit || ioreq->iouh_DevAddr != 0 || unit->hu_RootHubAddr == 0)
        return 0;
    return unit->hu_ResetPort;
}

/*
 * Set up the endpoint of a transfer on the controller, which Poseidon of AROS
 * asks for through UHA_PrepareEndpoint when it opens a pipe; MorphOS's
 * Poseidon has no such call, so the driver does it at the first transfer.
 * xhciPrepareEndpoint() keeps its endpoint context in iouh_DriverPrivate2,
 * which isochronous requests use for their own data, so it works on a copy
 * (the requests of a real-time iso handler carry no unit either). The
 * context stays with the device (dc_EPContexts) without a reference of the
 * copy, and goes with the device.
 */
BOOL xhciMorphOSPrepareEndpoint(struct PCIController *hc, struct IOUsbHWReq *ioreq, ULONG txtype) {
    struct IOUsbHWReq req = *ioreq;
    struct pciusbXHCIEndpointCtx *epctx;
    LONG err;

    req.iouh_Req.io_Unit = (struct Unit *)hc->hc_Unit;
    req.iouh_Req.io_Command = txtype;
    req.iouh_DriverPrivate1 = NULL;
    req.iouh_DriverPrivate2 = NULL;
    err = xhciPrepareEndpoint(&req);
    if (err != UHIOERR_NO_ERROR) {
        pciusbWarn("xHCI", "cannot set up endpoint %lu %s of device %lu: error %ld\n",
                   (ULONG)ioreq->iouh_Endpoint, (ioreq->iouh_Dir == UHDIR_IN) ? "IN" : "OUT",
                   (ULONG)ioreq->iouh_DevAddr, err);
        return FALSE;
    }

    epctx = (struct pciusbXHCIEndpointCtx *)req.iouh_DriverPrivate2;
    if (epctx) {
        Disable();
        if (epctx->ectx_RefCount)
            epctx->ectx_RefCount--;
        Enable();
    }
    return TRUE;
}

/* A request for the endpoint epid of device devaddr waits in one of the queues. */
static BOOL xhciMorphOSEndpointQueued(struct PCIController *hc, UWORD devaddr, ULONG epid) {
    struct List *queues[] = {&hc->hc_TDQueue, &hc->hc_PeriodicTDQueue, &hc->hc_IntXFerQueue,
                             &hc->hc_IsoXFerQueue, &hc->hc_BulkXFerQueue};
    struct IOUsbHWReq *ioreq;
    ULONG q;

    for (q = 0; q < sizeof(queues) / sizeof(queues[0]); q++) {
        ForeachNode(queues[q], ioreq) {
            if ((ioreq->iouh_DevAddr == devaddr) &&
                (xhciEndpointID(ioreq->iouh_Endpoint, (ioreq->iouh_Dir == UHDIR_IN) ? 1 : 0) == epid))
                return TRUE;
        }
    }
    return FALSE;
}

/*
 * SET_INTERFACE changes the endpoints of an interface, and AROS's Poseidon
 * destroys and prepares them around it (UHA_DestroyEndpoint and
 * UHA_PrepareEndpoint); MorphOS's Poseidon does not. After a successful
 * SET_INTERFACE, drop the device's endpoints other than EP0 that have no
 * transfer on the ring or in a queue: the next transfer sets each up again,
 * with the packet size of the new alternate setting and an empty ring.
 * devCtx is a real device, not the root hub.
 */
void xhciMorphOSSetInterfaceDone(struct PCIController *hc, struct IOUsbHWReq *ioreq, struct pciusbXHCIDevice *devCtx) {
    ULONG epid;

    if ((ioreq->iouh_SetupData.bmRequestType != (URTF_STANDARD | URTF_INTERFACE)) ||
        (ioreq->iouh_SetupData.bRequest != USR_SET_INTERFACE))
        return;

    for (epid = 2; epid < MAX_DEVENDPOINTS; epid++) {
        struct pcisusbXHCIRing *ring = devCtx->dc_EPAllocs[epid].dmaa_Ptr;
        struct IOUsbHWReq req;
        BOOL busy = FALSE;
        ULONG idx;

        if (!ring)
            continue;
        Disable();
        for (idx = 0; ring->ringio && (idx < XHCI_EVENT_RING_TRBS) && !busy; idx++)
            busy = (ring->ringio[idx] != NULL);
        if (!busy)
            busy = xhciMorphOSEndpointQueued(hc, ioreq->iouh_DevAddr, epid);
        Enable();
        if (busy)
            continue;

        memset(&req, 0, sizeof(req));
        req.iouh_Req.io_Unit = (struct Unit *)hc->hc_Unit;
        req.iouh_DevAddr = ioreq->iouh_DevAddr;
        req.iouh_Endpoint = epid >> 1;
        req.iouh_Dir = (epid & 1) ? UHDIR_IN : UHDIR_OUT;
        xhciDestroyEndpoint(&req);
    }
}

#undef base
#define base (hc->hc_Device)

/* Interrupt code of the controller, called natively with the controller. */
BOOL xhciMorphOSAddInterrupt(struct PCIController *hc, ULONG (*code)(struct PCIController *)) {
    hc->hc_PCIXIntObject = PCIXCreateIntObjectTags(hc->hc_PCIDeviceObject, (ULONG(*)(void))code, hc,
                                                   PCIXINTTAG_MACHINE, MACHINE_PPC,
                                                   PCIXINTTAG_PRI, 5,
                                                   TAG_END);
    return hc->hc_PCIXIntObject != NULL;
}

void xhciMorphOSRemInterrupt(struct PCIController *hc) {
    if (hc->hc_PCIXIntObject) {
        PCIXDeleteIntObject(hc->hc_PCIXIntObject);
        hc->hc_PCIXIntObject = NULL;
    }
}

/*
 * Memory the controller reaches by DMA. pcix.library gives the physical
 * address of a CPU address; the way back (event TRBs point into the rings)
 * goes through a record of every allocation, so it does not depend on how
 * MorphOS maps memory.
 */
#define XHCI_MORPHOS_DMA_RECORDS 512
static struct {
    UBYTE *virt;
    IPTR phys;
    ULONG size;
} dmaRecords[XHCI_MORPHOS_DMA_RECORDS];
static ULONG dmaLogged = 0;

APTR xhciMorphOSAllocPCIMem(struct PCIController *hc, ULONG size) {
    UBYTE *mem = AllocVec(size, MEMF_PUBLIC | MEMF_CLEAR);
    ULONG i;
    if (!mem)
        return NULL;
    Disable();
    for (i = 0; i < XHCI_MORPHOS_DMA_RECORDS; i++) {
        if (!dmaRecords[i].virt) {
            dmaRecords[i].virt = mem;
            dmaRecords[i].phys = (IPTR)PCIXDMAGetPhysical(hc->hc_PCIDeviceObject, mem);
            dmaRecords[i].size = size;
            break;
        }
    }
    Enable();
    if (i == XHCI_MORPHOS_DMA_RECORDS) {
        kprintf("pcixhci: too many DMA allocations\n");
        FreeVec(mem);
        return NULL;
    }
    if (dmaLogged < 4) {
        dmaLogged++;
        kprintf("pcixhci: DMA memory %lu bytes at 0x%08lx, physical 0x%08lx (last byte 0x%08lx)\n", size, (IPTR)mem,
                dmaRecords[i].phys, (IPTR)PCIXDMAGetPhysical(hc->hc_PCIDeviceObject, mem + size - 1));
    }
    return mem;
}

void xhciMorphOSFreePCIMem(struct PCIController *hc, APTR address) {
    ULONG i;
    (void)hc;
    Disable();
    for (i = 0; i < XHCI_MORPHOS_DMA_RECORDS; i++) {
        if (dmaRecords[i].virt == address) {
            dmaRecords[i].virt = NULL;
            break;
        }
    }
    Enable();
    FreeVec(address);
}

APTR xhciMorphOSCPUtoPCI(struct PCIController *hc, APTR address) {
    return PCIXDMAGetPhysical(hc->hc_PCIDeviceObject, address);
}

APTR xhciMorphOSPCItoCPU(struct PCIController *hc, APTR address) {
    IPTR phys = (IPTR)address;
    ULONG i;
    (void)hc;
    for (i = 0; i < XHCI_MORPHOS_DMA_RECORDS; i++) {
        if (dmaRecords[i].virt && phys >= dmaRecords[i].phys && phys - dmaRecords[i].phys < dmaRecords[i].size)
            return dmaRecords[i].virt + (phys - dmaRecords[i].phys);
    }
    kprintf("pcixhci: no CPU address for DMA address 0x%08lx\n", phys);
    return NULL;
}

/* /// "pciAllocAligned()" */
APTR pciAllocAligned(struct PCIController *hc, struct MemEntry *alloc, ULONG Size, ULONG align, ULONG bounds) {
    alloc->me_Length = align + Size;
    alloc->me_Un.meu_Addr = ALLOCPCIMEM(hc, hc->hc_PCIDriverObject, alloc->me_Length);
    if (alloc->me_Un.meu_Addr) {
        IPTR addrAligned = (((IPTR)alloc->me_Un.meu_Addr + (align - 1)) & ~(align - 1));
        if ((addrAligned & (bounds - 1)) != ((addrAligned + Size - 1) & (bounds - 1))) {
            FREEPCIMEM(hc, hc->hc_PCIDriverObject, alloc->me_Un.meu_Addr);
            alloc->me_Length = bounds + Size;
            alloc->me_Un.meu_Addr = ALLOCPCIMEM(hc, hc->hc_PCIDriverObject, alloc->me_Length);
            addrAligned = (((IPTR)alloc->me_Un.meu_Addr + (bounds - 1)) & ~(bounds - 1));
        }
        if (alloc->me_Un.meu_Addr)
            return (APTR)addrAligned;
    }
    kprintf("pcixhci: unable to allocate %lu bytes of aligned memory\n", Size);
    return NULL;
}
/* \\\ */

/* /// "pciFreeAligned()" */
void pciFreeAligned(struct PCIController *hc, struct MemEntry *alloc) {
    if (!alloc->me_Un.meu_Addr)
        return;
    FREEPCIMEM(hc, hc->hc_PCIDriverObject, alloc->me_Un.meu_Addr);
    alloc->me_Un.meu_Addr = NULL;
    alloc->me_Length = 0;
}
/* \\\ */

/* /// "pciGetPhysical()" */
APTR pciGetPhysical(struct PCIController *hc, APTR virtaddr) {
    return xhciMorphOSCPUtoPCI(hc, virtaddr);
}
/* \\\ */
