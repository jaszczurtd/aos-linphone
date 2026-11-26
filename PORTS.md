# Ports to other Amiga systems

aos-linphone runs on AROS x86_64 and on MorphOS. The aim is every Amiga
system, so AmigaOS 3.x on 68k processors and AmigaOS 4 come next. This
document describes the state of the MorphOS port and collects what we know
about the others as of November 2026: what carries over from AROS, what
blocks each system, and the order we plan to take.
Statements marked *(checked)* were verified in code, headers or a test run;
*(estimate)* marks our own judgement.

## In short

| | MorphOS | AmigaOS 3.x | AmigaOS 4.1 |
|---|---|---|---|
| Processor | PowerPC G3/G4/G5, big-endian | 68020-68060, Vampire 68080, emulators with JIT | PowerPC (X5000, A1222, Sam460, Pegasos), big-endian |
| Compiler | GCC 15.2 in SDK 3.20, also as a cross compiler from pkgsrc *(checked)*; pthreads work, `std::thread::join` and `thread_local` destructors have bugs | GCC 16.2 (AmigaPorts), C++ threads only in a self-built toolchain | GCC 11/13/15 (adtools), clib4 with pthreads |
| Network API | AmiTCP level: no `getaddrinfo`, sockets per task | Roadshow: `getaddrinfo`, shared library bases | Roadshow: `getaddrinfo`, shared library bases |
| USB stack | Poseidon, like AROS | Poseidon (commercial) on USB cards | its own stack, not Poseidon |
| USB camera | AROS `uvc.class` and xHCI driver ported; pictures in QEMU, slowly *(checked)* | only real hardware, MJPEG too costly | `usb2video.device` of the community `usb2` stack |
| Emulation | QEMU `pegasos2` boots MorphOS 3.21 *(checked)* | WinUAE, FS-UAE, Amiberry (JIT) | QEMU `pegasos2`, `amigaone`, `sam460ex` |
| Verdict | runs in QEMU; real hardware untested | doable for JIT emulators and fast accelerators, not for a real 68060 | doable, camera through `usb2` only |

Order: MorphOS first (running), then AmigaOS 3.x for emulators and fast
accelerators, then AmigaOS 4.

## Why AROS was the easy start

AROS looks like the poorest of the family, but for this port it offered a
lot:

- GCC 10.5 with POSIX threads, so the C++17 linphone stack with
  `std::thread`, mutexes and atomics builds as it is.
- Its C library hands `read`, `write` and `close` on sockets to the network
  stack through `fd.library`.
- AROSTCP has the Roadshow API: `getaddrinfo`, a socket library base shared
  by all threads of a program, and an errno hook.
- `uvc.class` drives USB cameras and publishes them as VHI libraries.
- A current x86_64 core is one to two orders of magnitude faster than any of
  the other targets.

The other systems are 32-bit and big-endian, and each lacks some of the
above.

## What all ports share

### Size

The `aos-phone` file has 31 MB, but most of that are relocations and symbols
of the relocatable AROS executable. What is loaded is about 12.5 MB: 9.4 MB
of code, 1.5 MB of unwind tables, 1.1 MB of constants *(checked)*. With
buffers for sound and video, a machine needs some 64 MB of free memory.

### Big-endian

Most of the stack handles big-endian processors, but three places need
care *(checked in the sources)*:

- oRTP defines `ORTP_BIGENDIAN` only in its private config header, while the
  public `ortp/rtp.h` lays out the bit fields of the RTP header from it, and
  mediastreamer2 reads those fields (marker bit, payload type). On a
  big-endian machine they are read wrongly, which breaks video frames.
  Patch 0009 takes the byte order from the compiler in the public headers.
- mediastreamer2's WAV recorder swaps bytes only for L16 payloads, so call
  recordings get the wrong byte order. Patch 0010 fixes it.
- belle-sip's DNS code picks the layout of the DNS header bit fields from
  `BYTE_ORDER`. The MorphOS headers define it (`<machine/endian.h>`, through
  `<sys/types.h>`); on other systems the compatibility layer may have to.

libvpx detects big-endian in its configure script, mbedtls takes the order
from `__BYTE_ORDER__`, and the libyuv paths we use write byte by byte. Our
own device code works unchanged: AHI takes 16-bit samples in processor
order, and the picture paths work on bytes. Raw test sound files have to be
prepared in big-endian order (`s16be`).

A run of the stack on big-endian Linux (PowerPC in QEMU), without any Amiga
API, confirmed the first two *(checked)*: without the patches, `linphonec`
on PowerPC drops the video packets of a call with an `x86_64` one as
packets of a wrong payload type, and with patch 0009 alone its recordings
have their bytes swapped. With both patches the two call each other with
sound and VP8 video, and the recordings match what the other side sent.

### Code each port changes

The MorphOS port has done each of these; the next ports follow the same
pattern.

- `compat/` has a common part and one backend per system
  (`src/net/<system>/`, `src/misc/<system>/`, `include/aos/compat_<system>.h`):
  socket calls, name resolution, `close`/`read`/`write` on sockets where the
  C library does not pass them on, and, on 68k, `clock_gettime` and an
  entropy source.
- The GUI uses the `SDI_hook.h` macros for hooks and dispatchers
  (`apps/aos-phone/sdi/`), which cover all systems, and `mui_compat.h` for
  the few differences between Zune and MUI. The MUI classes it uses exist
  from MUI 3.8 on.
- The patches for bctoolbox and liblinphone that name AROS need the macros
  of the other systems.
- `vhi_camera.c` uses AROS's VHI extensions for UVC modes; on MorphOS it
  compiles against the headers of the port in `drivers/uvc/`.
- Each system needs a CMake toolchain file. libvpx and libjpeg-turbo build
  without x86 assembly, and systems without an FPU need fixed-point codecs.

## MorphOS

**Compiler.** The best of the three. SDK 3.20 (May 2026) has GCC 15.2, a
GCC thread model for MorphOS, exceptions and TLS in the kernel since 3.14,
but as MorphOS programs. On Linux, pkgsrc builds the same GCC 15.2 with
the same MorphOS patches as a cross compiler (`cross/ppc-morphos-gcc`, with
binutils 2.45.1 and SDK 3.20), and its programs run in MorphOS 3.21
*(checked)*. Programs with MUI need the libnix C library: the SDK forbids
MUI and system libraries with ixemul. POSIX threads from libpthread work
with both C libraries *(checked)*. Two problems of C++ threads remain
*(checked)*:

- `std::thread::join()` fails with EINVAL when the thread has already
  ended. The MorphOS thread code reads the new thread's id only after the
  thread may have run to its end; reading it earlier fixes it in our tests.
  In the linphone stack only mediastreamer2's TURN over TCP uses
  `std::thread`.
- Destructors of `thread_local` objects do not run when a thread ends,
  while destructors of `pthread_key_create` keys do. bctoolbox and
  liblinphone have such objects. The compiler's default
  `-fno-use-cxa-atexit` is the cause: the port builds all C++ code with
  `-fuse-cxa-atexit` and supplies the `__cxa_atexit()` the C library lacks,
  and then the destructors run *(checked)*.

**Network: the main obstacle.** The MorphOS socket library is on the level
of AmiTCP: no `getaddrinfo`, `inet_pton` or `inet_ntop`, no IPv6, no shared
library bases and no errno hook, and socket descriptors belong to a task.
liblinphone creates its RTP sockets in the core thread but sends and
receives in the media threads (MSTicker). A test program shows how the two
C libraries behave *(checked)*:

- with libnix, a socket of one thread gives `EBADF` in another, while a copy
  handed over with `ReleaseCopyOfSocket` and taken with `ObtainSocket` in a
  thread with its own library base works in both directions;
- ixemul copies the descriptors into a thread when it is created, so later
  sockets of either thread stay invisible to the other;
- `MSG_DONTWAIT` is ignored, `FIONBIO` works, and errno is per thread.

Copies handed over this way share the socket's state, and closing one copy
leaves the socket open while another exists *(checked)*. The MorphOS
backend of the compatibility layer gives each socket a number of its own
and each thread its own copy, taken on first use; closing a socket shuts it
down for every thread. Name resolution is built on `gethostbyname`, IPv4
only. The layer's tests pass in MorphOS 3.21 *(checked)*. The network stack
does not report the DNS servers it got from DHCP, so without servers set by
hand the SIP resolver takes addresses from `getaddrinfo` and cannot look up
SRV records.

**Status** *(checked in QEMU)*. The SDK builds for MorphOS with libnix. The
console test program registers an account on a test registrar, the phone
starts with its MUI window and ARexx port, and a video call between the
phone on MorphOS and the phone on AROS shows the picture of the other side
on both. Calls with linphonec on Linux carry video and sound from files in
both directions, and a call recorded on MorphOS matches the sound the other
side sent. Under emulation the Opus encoder does not keep up, so sound sent
from MorphOS has gaps; with PCMU it is clean. HTTPS (mbedtls) and name
resolution work. Sound cards are untested, because the emulated Pegasos II
has no AHI driver. A USB camera's picture reaches the other side of a
call, but under emulation only every few seconds, and in some calls too
late for mediastreamer2, which then shows a placeholder (see the camera
below).

**Camera: a port of the AROS class.** MorphOS uses the Poseidon USB stack,
as AROS does. AROS's `uvc.class` (about 2,400 lines, AROS Public Licence)
is an ordinary Poseidon class: bulk or isochronous streaming through
Poseidon's real-time isochronous API, YUY2 and MJPEG, VHI libraries for the
cameras. The port in `drivers/uvc/` keeps its USB code and replaces the
AROS macros, its logging and the library skeleton; it leaves out MJPEG,
for which MorphOS has no JPEG decoder library, and USB 3. On the
EHCI controller of MorphOS 3.21 the camera binds and negotiates a stream,
but no picture arrives: the system's USB host driver refuses a real-time
isochronous handler there, and plain isochronous transfers never complete
*(checked)*.

So the port brings its own host controller driver as well: AROS's
`pcixhci.device` (xHCI, AROS Public Licence) in `drivers/xhci/`. MorphOS has
no xHCI driver, so it takes over a controller nothing else uses. The port
keeps the AROS code and adds what MorphOS lacks around it: PCI access
through `pcix.library`, the device skeleton, port status words in the byte
order Poseidon 4 expects, the root port of a new device, and endpoints set
up at their first transfer and again after `SET_INTERFACE`, which AROS's
Poseidon asks for itself. It also needed fixes for big-endian machines,
and for an isochronous ring that the driver lost track of under load,
which AROS's own builds would need too; `uvc.class` no longer stops a
stream while a grab waits for a slow frame. In QEMU with its xHCI and the
camera passed through, the camera enumerates, `uvc.class` delivers 320x240
pictures, and in a video call linphonec on Linux receives the camera's
picture from the phone *(checked)*. Under software emulation the processor
completes about 3,000 of the camera's 8,000 isochronous packets a second,
so many frames break and a whole one takes from five seconds to half a
minute. The phone keeps a camera that slow as long as it delivers, but
mediastreamer2 shows its placeholder when the first picture of a call has
not come within about 15 seconds, which happened in some calls; at
640x480 no picture came in time at all. A camera on real hardware was not
tried.

**Sound.** The built-in audio of PowerMacs does not record; other cards and
USB audio probably do *(estimate)*.

**Processor.** A G4 at 1.4-1.67 GHz or a G5. libvpx has no AltiVec code, so
VP8 at CIF size is near the limit, about 10-15 pictures per second
*(estimate)*; QCIF is safe.

**Emulation** *(checked)*. MorphOS 3.21 boots to the Ambient desktop in QEMU
8.2 on the `pegasos2` machine (software emulation, no hardware assistance on
x86), installs to a disk and gets a network address by DHCP. What we learned:

- QEMU has no Pegasos firmware; it loads `boot.img` from the top of the
  MorphOS CD as the kernel, which then starts the system from the CD or,
  with the drive empty, from the disk.
- The board has two PCI buses, and interrupts work only on the one with the
  VIA south bridge. A network card on the other bus sends but never reports
  received packets: DHCP requests go out, the offers never arrive.
- A USB tablet works on the board's own USB controller, not on an added
  Intel one, and MorphOS scales its range beyond the screen.
- A restart from inside MorphOS does not come back: the system hands over to
  the firmware, which QEMU replaces with a minimal one, and the machine
  stops. It has to be reset from outside.
- No AHI mode for the board's AC97 sound came with the installed system.
- Unregistered MorphOS slows down after 30 minutes until the next restart.

## AmigaOS 3.x (68k)

### Why a 68060 is not enough

A call is real-time work on several fronts at once. Every 20 ms the phone
resamples the microphone, cancels the echo, encodes and encrypts the sound,
and on the other side takes packets from the jitter buffer, decodes, mixes
and resamples them; video adds encoding, decoding and colour conversion of
every picture, and the C++ SIP stack parses every message with grammar-
driven parsers. On AROS in a virtual machine on a current x86 core, echo
cancellation alone took a quarter of the audio thread's time, and decoding
VP8 at 1280x720 from a desktop client took the whole processor until the
phone limited the bandwidth it announces.

A 68060 runs at 50-100 MHz, has no SIMD unit, and its FPU traps several
instructions of the 68881/68882 to software. As a yardstick, a 68060 at
50 MHz needs about 45% of its time just to decode MP3 (AmigaAMP's
measurements). Opus has to encode and decode at the same time, plus echo
cancellation: more than the processor has *(estimate)*. G.711 and Speex at
8 kHz are realistic, Opus only at its lowest complexity, VP8 at most QCIF at
a few pictures per second. The parsers and the C++ stack itself were never
measured on a 68060.

### JIT emulation and fast accelerators change that

WinUAE, FS-UAE and Amiberry translate 68k code into host code with a JIT,
and Emu68 does the same on a Raspberry Pi (PiStorm). This runs 68k programs
many times faster than any 68060. There is no reliable benchmark, as SysInfo
inflates JIT results, so the codecs have to be measured on the target. With
plenty of Fast RAM in an emulator, memory is no limit either. The processor
stops being the main obstacle; two others remain:

- **Compiler and C library.** The AmigaPorts toolchain (GCC 16.2) offers
  `std::thread` only when built with `THREADS=amigaos`; its released
  binaries are built without threads, and then `<thread>` and `<mutex>` do
  not compile. newlib with `-pthread` has a thread-safe `malloc` and stdio
  but no sockets and no `clock_gettime`; libnix has sockets and
  `clock_gettime`, but its stdio is not thread-safe; clib2 and ixemul have
  no threads. newlib with our compatibility layer supplying sockets over
  bsdsocket.library and the clock over timer.device fits the current design
  best.
- **Network in emulators.** WinUAE's emulated `bsdsocket.library` has no
  `getaddrinfo` and rejects the socket-base tags for an errno hook and for
  sharing the library base. The guest needs Roadshow (version 4.275 and
  later has both) with an emulated network card such as the A2065.

Emulators have no USB, so no camera there: the test camera and file sources
remain. Whether the emulators record through AHI still has to be checked.
On real hardware with a USB card, the commercial Poseidon 4.3 has a video
class for MJPEG cameras, but decoding MJPEG is too costly for a 68060.

A 68k build would also run on AmigaOS 4 under its 68k emulation, whose
socket library has the needed API, but not on MorphOS, whose stack lacks it.

### Dual-processor accelerators: WarpOS and PowerUP

Phase5's BlizzardPPC (A1200, PowerPC 603e at 160-240 MHz) and CyberStorm
PPC (A3000/A4000, 604e at 150-233 MHz) carry a 68040 or 68060 next to the
PowerPC, which runs code under WarpOS (Haage & Partner, `powerpc.library`)
or PowerUP (Phase5, `ppc.library`). The PowerPC is several times faster than
the 68060, mostly in floating point, but has no AltiVec *(estimate)*.

Moving the whole phone to the PowerPC does not work:

- AmigaOS 3.x itself runs only on the 68k. Every system call from PowerPC
  code (sockets, AHI, timers, MUI) switches processors, and each switch
  flushes the caches of both. liblinphone and mediastreamer2 call the
  system all the time, so such a build would spend its time switching.
- No toolchain for these systems has C++17 with threads. vbcc compiles C
  for both; `gcc-mos2wos` offers GCC 9 for C and C++ to WarpOS, with an
  unknown thread model.
- WarpOS and PowerUP are separate systems with separate binaries.

What can work is splitting the media path *(estimate)*: everything that
needs the system stays on the 68k (GUI, SIP, sockets, AHI), and a long-
running PowerPC task does pure computation in C: Opus or Speex encoding and
decoding, echo cancellation, resampling, and VP8 with colour conversion. The
two sides exchange buffers through rings in shared memory, with explicit
cache flushes and one wake-up per frame instead of a processor switch for
every filter call. In mediastreamer2 that means proxy filters that hand
their work to the PowerPC and take the result one tick later (10-20 ms more
latency); liblinphone itself does not change. Sound would then be realistic
on these boards and video at small sizes; how much the SIP stack and C++
runtime cost on a 68060 still has to be measured, since they stay there.

This builds on the 68k port (everything but the computation runs there)
and helps only real hardware: under JIT emulation the 68k side is already
fast. WinUAE and FS-UAE can emulate these boards through a QEMU-based
PowerPC plugin, which would serve for tests *(to check)*.

## AmigaOS 4.1

**Compiler.** AmigaLabs adtools with GCC 11, 13 or 15 (also as Docker
images with SDK 54.25 and MUI 5). clib4 has pthreads, `clock_gettime`,
`poll`, sockets in the descriptor table, and a socket interface with shared
library bases and the errno hook: the same model as on AROS. Toolchains
older than September 2026 had a memory corruption in
`condition_variable::wait_for` with clib4. clib4's `getaddrinfo` rejects
some flags, so our layer would call Roadshow directly.

**Network.** The socket library is Roadshow, with full `getaddrinfo` since
version 4.351; no IPv6.

**USB: a different stack.** AmigaOS 4 has its own USB stack, not Poseidon,
so the AROS camera class does not carry over without rewriting its USB
layer. Isochronous transfers, which cameras and USB audio need, work since
2024, and a USB Audio Class driver with recording appeared in 2026 (beta).
The community stack `usb2` (GPL-3.0) brings `usb2video.device` with UVC
cameras in MJPEG and YUY2; on an X5000, MJPEG at 640x480 worked. A camera
source for aos-linphone on that device would be small, with MJPEG decoded by
libjpeg-turbo, which the SDK already has, but users would have to replace
the system's USB stack with `usb2`.

**Sound.** HD Audio 6.11 (X1000, X5000, Sam460) and SB128 record.

**Processor.** The X5000 (e5500, 2 GHz) has no AltiVec, so VP8 runs in plain
C; CIF at 15 pictures per second is plausible *(estimate)*. The A1222
(e500v2) has no classic FPU and needs fixed-point codecs.

**Emulation.** QEMU's `pegasos2`, `amigaone` and `sam460ex` machines.

## Plan

Done: the big-endian check on Linux PowerPC, the split of `compat/`, the
GUI on `SDI_hook.h`, and the MorphOS port with its network backend, SDK,
phone, `uvc.class` and xHCI driver. On MorphOS, sound cards and a USB
camera on real hardware remain untested. Next:

1. AmigaOS 3.x for JIT emulators and fast accelerators: self-built GCC with
   threads and newlib, a Roadshow network backend, codec measurements; later,
   optionally, the media computation on the PowerPC of WarpOS or PowerUP
   boards.
2. AmigaOS 4: adtools with clib4, a network backend on its socket interface,
   camera through `usb2video.device`.

## Sources

- MorphOS: [SDK and news](https://www.morphos-team.net/news),
  [socket library](https://morphos-team.net/sdk/socket.html),
  [socket base tags](https://morphos-team.net/sdk/includes/net/socketbasetags.html),
  [hardware list](https://morphos-team.net/hardware), [FAQ](https://www.morphos-team.net/faq),
  [Wayfarer](https://github.com/jacadcaps/webkitty)
- QEMU and Amiga-like systems: [BALATON Zoltan's notes](https://zero.eik.bme.hu/~balaton/qemu/amiga/)
- AmigaOS 3.x: [AmigaPorts m68k-amigaos-gcc](https://github.com/AmigaPorts/m68k-amigaos-gcc)
  ([runtimes](https://github.com/AmigaPorts/m68k-amigaos-gcc/blob/master/doc/runtimes.md),
  [build](https://github.com/AmigaPorts/m68k-amigaos-gcc/blob/master/doc/build.md)),
  [Roadshow includes](https://github.com/AmigaPorts/amiga-netinclude),
  [WinUAE bsdsocket](https://github.com/tonioni/WinUAE/blob/master/bsdsocket.cpp),
  [AmigaAMP AHI results](https://amigaamp.de/AHIresults.shtml),
  [WarpOS](https://en.wikipedia.org/wiki/WarpOS),
  [vbcc for WarpOS](https://aminet.net/package/dev/c/vbcc_target_ppc-warpos),
  [gcc-mos2wos](https://aminet.net/package/dev/gcc/gcc-mos2wos),
  [JIT and SysInfo](https://nonarkitten.github.io/what-is-quicker)
- AmigaOS 4: [adtools](https://github.com/AmigaLabs/adtools),
  [clib4](https://github.com/AmigaLabs/clib4),
  [Docker images](https://github.com/walkero-gr/AmigaGCConDocker),
  [bsdsocket autodoc](https://wiki.amigaos.net/amiga/autodocs/bsdsocket.doc.txt),
  [usb2 stack](https://github.com/rolsen74/usb2),
  [USB Audio Class driver](https://www.amiga-news.de/en/news/AN-2026-03-00046-EN.html)
- AROS: `rom/usb/classes/video` (uvc.class), `rom/usb/pcixhci`
  (pcixhci.device) and `rom/usb/poseidon` in the
  [AROS sources](https://github.com/aros-development-team/AROS)
