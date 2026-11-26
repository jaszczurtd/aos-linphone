# pcixhci.device for MorphOS: the AROS USB 3 host controller driver (xHCI),
# ported to MorphOS. MorphOS has no xHCI driver of its own; this one gives
# Poseidon a controller whose high-speed ports carry isochronous transfers,
# which USB cameras (uvc.class in drivers/uvc) need. A separate project,
# built for MorphOS only:
#
#   cmake -S drivers/xhci -B build-xhci -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/morphos-ppc.cmake \
#         -DMORPHOS_TOOLCHAIN_DIR=<directory with bin/ppc-morphos-gcc>
#   cmake --build build-xhci
#
# The result, build-xhci/pcixhci.device, goes to SYS:Devs/USBHardware, and
# AddUSBHardware hands it to Poseidon (see drivers/README.md).
#
# Licensed under the AROS Public License version 1.1, like the AROS driver
# it comes from (see LICENCE.md of the repository).

cmake_minimum_required(VERSION 3.16)
project(pcixhci_device LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "MorphOS")
    message(FATAL_ERROR "drivers/xhci builds pcixhci.device for MorphOS; use cmake/morphos-ppc.cmake")
endif()

add_executable(pcixhci_device
    buffer.c
    debug.c
    pcixhci_dev.c
    uhwcmd.c
    xhci_async.c
    xhci_controllerclass.c
    xhci_hcd.c
    xhci_hcport.c
    xhci_hw.c
    xhci_isoch.c
    xhci_periodic.c
    xhci_schedule.c
    xhcidebug.c
    xhcidevice.c
    morphos/pcixhci_morphospci.c)
set_target_properties(pcixhci_device PROPERTIES OUTPUT_NAME pcixhci.device)
# morphos/ holds the MorphOS side: PCI access through pcix.library, the
# device's Resident and function table, and stand-ins for the AROS headers.
target_include_directories(pcixhci_device PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/morphos
    ${CMAKE_CURRENT_SOURCE_DIR}/morphos/include)
# The options AROS builds the driver with (rom/usb/pcixhci/make.opts):
# isochronous transfers, inline command helpers, 64-bit pointer writes.
target_compile_definitions(pcixhci_device PRIVATE
    "LC_LIBDEFS_FILE=\"pcixhci_libdefs.h\""
    PCIUSB_WIP_ISO
    PCIUSB_INLINEXHCIOPS
    PCIUSB_XHCIUSESETPTR)
# The SDK's inline tag-list macros put pointers into ULONG arrays. The AROS
# sources carry #warning notes, keep variables and names for their debug
# builds and pass const pointers to CopyMem(), which takes APTR on MorphOS.
target_compile_options(pcixhci_device PRIVATE
    -O2 -g1 -Wall -Wno-int-conversion -Wno-cpp
    -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function
    -Wno-unused-const-variable -Wno-discarded-qualifiers -Wno-pointer-sign
    -include ${CMAKE_CURRENT_SOURCE_DIR}/morphos/morphos_compat.h)
# xhci_hcd.c reads dwords 2 and 3 of an endpoint context through ctx[2]
# (the dequeue pointer that follows in struct xhci_ep), and on 32-bit
# targets xhciSetPointer() expands to two statements, the second clearing a
# high half that is 0 anyway.
set_source_files_properties(xhci_hcd.c PROPERTIES
    COMPILE_OPTIONS "-Wno-array-bounds;-Wno-multistatement-macros")
# A device: no start-up code; exec finds it through its Resident. libdebug
# gives kprintf (serial port) for the driver's log.
target_link_options(pcixhci_device PRIVATE -nostartfiles)
target_link_libraries(pcixhci_device PRIVATE -ldebug)
