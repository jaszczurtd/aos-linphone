# Ports to other Amiga systems

aos-linphone runs on AROS x86_64 today. The aim is every Amiga system:
MorphOS, AmigaOS 3.x on 68k processors and AmigaOS 4. This document
collects what we know about those ports as of November 2026: what carries
over from AROS, what blocks each system, and the order we plan to take.
Statements marked *(checked)* were verified in code, headers or a test run;
*(estimate)* marks our own judgement.

## In short

| | MorphOS | AmigaOS 3.x | AmigaOS 4.1 |
|---|---|---|---|
| Processor | PowerPC G3/G4/G5, big-endian | 68020-68060, Vampire 68080, emulators with JIT | PowerPC (X5000, A1222, Sam460, Pegasos), big-endian |
| Compiler | GCC 15.2 in SDK 3.20, threads and TLS | GCC 16.2 (AmigaPorts), C++ threads only in a self-built toolchain | GCC 11/13/15 (adtools), clib4 with pthreads |
| Network API | AmiTCP level: no `getaddrinfo`, sockets per task | Roadshow: `getaddrinfo`, shared library bases | Roadshow: `getaddrinfo`, shared library bases |
| USB stack | Poseidon, like AROS | Poseidon (commercial) on USB cards | its own stack, not Poseidon |
| USB camera | port of AROS `uvc.class` looks easy | only real hardware, MJPEG too costly | `usb2video.device` of the community `usb2` stack |
| Emulation | QEMU `pegasos2` boots MorphOS 3.21 *(checked)* | WinUAE, FS-UAE, Amiberry (JIT) | QEMU `pegasos2`, `amigaone`, `sam460ex` |
| Verdict | doable; the network layer is the main work | doable for JIT emulators and fast accelerators, not for a real 68060 | doable, camera through `usb2` only |

Planned order: MorphOS first, then AmigaOS 3.x for emulators and fast
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
fixing *(checked in the sources)*:

- oRTP defines `ORTP_BIGENDIAN` only in its private config header, while the
  public `ortp/rtp.h` lays out the bit fields of the RTP header from it, and
  mediastreamer2 reads those fields (marker bit, payload type). On a
  big-endian machine they would be read wrongly, breaking video frames. The
  SDK has to be built with `ORTP_BIGENDIAN` for every component.
- mediastreamer2's WAV recorder swaps bytes only for L16 payloads, so call
  recordings would have the wrong byte order. It needs a patch.
- belle-sip's DNS code picks the layout of the DNS header bit fields from
  `BYTE_ORDER`; the compatibility layer has to define it.

libvpx detects big-endian in its configure script, mbedtls takes the order
from `__BYTE_ORDER__`, and the libyuv paths we use write byte by byte. Our
own device code works unchanged: AHI takes 16-bit samples in processor
order, and the picture paths work on bytes. Raw test sound files have to be
prepared in big-endian order (`s16be`).

The first step for all three ports is therefore a run of the stack on big-
endian Linux (PowerPC in QEMU), without any Amiga API: two `linphonec`
instances calling each other with sound and VP8 video, plus a recording.

### Code that has to change

- `compat/` is AROS-only today (`aos_compat.h` stops with `#error` on other
  systems). It has to split into a common part and one backend per system:
  socket calls, name resolution, `close`/`read`/`write` on sockets where the
  C library does not pass them on, and, on 68k, `clock_gettime` and an
  entropy source.
- The GUI uses AROS macros for hooks and dispatchers; the `SDI_hook.h`
  macros, which AROS has too, cover all systems. The MUI classes it uses
  exist from MUI 3.8 on.
- The patches for bctoolbox and liblinphone that name AROS need the macros
  of the other systems.
- `vhi_camera.c` uses AROS's VHI extensions for UVC modes.
- Each system needs a CMake toolchain file. libvpx and libjpeg-turbo build
  without x86 assembly, and systems without an FPU need fixed-point codecs.

## MorphOS

**Compiler.** The best of the three. SDK 3.20 (May 2026) has GCC 15.2, a
GCC thread model for MorphOS, exceptions and TLS in the kernel since 3.14;
the WebKit browser Wayfarer shows that large C++ programs with threads work.

**Network: the main obstacle.** The MorphOS socket library is on the level
of AmiTCP: no `getaddrinfo`, `inet_pton` or `inet_ntop`, no IPv6, no shared
library bases and no errno hook, and socket data belong to the task that
opened the library. liblinphone creates its RTP sockets in the core thread
but sends and receives in the media threads (MSTicker), so sockets have to
move between tasks (`ReleaseCopyOfSocket`/`ObtainSocket` with a descriptor
map) or the port uses ixemul. A small test of that, like the first stage of
the AROS port, comes before anything else. Name resolution has to be built
on `gethostbyname`, IPv4 only.

**Camera: a port of the AROS class.** MorphOS uses the Poseidon USB stack,
as AROS does. AROS's `uvc.class` (about 2,400 lines, AROS Public Licence)
is an ordinary Poseidon class: bulk or isochronous streaming through
Poseidon's real-time isochronous API, YUY2 and MJPEG, VHI libraries for the
cameras. The MorphOS SDK 3.20 headers have that API and all 34 Poseidon
functions the class calls; only one AROS addition for USB 3 is missing, and
the SDK ships an example USB class as a starting point *(checked)*. The port
means replacing the AROS macros, its logging and the library skeleton, not
the USB code.

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

1. Big-endian check of the linphone stack on Linux PowerPC.
2. Split `compat/` into a common part and an AROS backend without changing
   behaviour; GUI on `SDI_hook.h`.
3. MorphOS: a first trial build and run in the MorphOS virtual machine to
   see what breaks, a test of sockets shared between tasks, then the network
   backend, the SDK, the phone, and the port of `uvc.class`.
4. AmigaOS 3.x for JIT emulators and fast accelerators: self-built GCC with
   threads and newlib, a Roadshow network backend, codec measurements; later,
   optionally, the media computation on the PowerPC of WarpOS or PowerUP
   boards.
5. AmigaOS 4: adtools with clib4, a network backend on its socket interface,
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
- AROS: `rom/usb/classes/video` (uvc.class) and `rom/usb/poseidon` in the
  [AROS sources](https://github.com/aros-development-team/AROS)
