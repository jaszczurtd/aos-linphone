# aos-linphone architecture

[Project overview](README.md) | [Contributing](CONTRIBUTING.md)

This document describes how aos-linphone is built: its layers, the
repository layout, the compatibility layer that lets the linphone stack run
on the system and the tests. Today every part targets AROS x86_64.

## Layers

The linphone stack is written for POSIX systems. aos-linphone keeps the
upstream code almost untouched and adds what the system lacks underneath
it. From the bottom up:

1. The compatibility layer (`compat/`) gives the stack a POSIX view of the
   AROS network API and the few system functions it needs, so the stack
   builds without changes for each AROS quirk. See
   [the compatibility layer](#using-the-compatibility-layer) below.
2. The libraries in `deps/` are ports of the third-party code the SDK
   needs: jsoncpp, speex, libxml2, mbedtls, libsrtp, opus, libvpx, libyuv
   and libjpeg-turbo.
3. The linphone stack itself (bctoolbox, belr, belle-sip, ortp,
   mediastreamer2 and liblinphone) comes from the upstream `linphone-sdk`,
   built with a few source changes from `patches/`. See
   [sdk/README.md](sdk/README.md).
4. The devices in `devices/` plug into mediastreamer2: an AHI sound card,
   a camera, a video display that hands call pictures to the user
   interface, and the Mire test camera, with the sources the card and the
   camera take their data from: the microphone on `ahi.device`, USB
   cameras through VHI, and raw files. See
   [Devices and sources](#devices-and-sources) below.
5. The programs in `apps/` link all of the above statically. `aos-phone`
   runs the linphone core in its own thread and talks to its Zune windows
   through fixed-size queues; its [README](apps/aos-phone/README.md)
   describes the structure.

### Devices and sources

The program registers its devices with the mediastreamer2 factory of the
linphone core, since AROS loads no mediastreamer2 plugins. The AHI card
and the camera run threads of their own while their streams run: the card
plays through a writer thread and captures through a reader thread, and
the camera grabs pictures in a thread too. These threads exchange data
with the MSTicker thread of the stream through a queue under a mutex, so
a slow device never holds up the ticker.

The camera and the microphone of the AHI card take their data from a
source (`devices/source.h`). A source only opens itself for a stream,
waits for the next picture or the next 20 ms of sound, and closes; the
device does the rest. There are three kinds:

- `devices/ahi/ahi_source.c` records a unit of `ahi.device` at 48 kHz mono.
  A thread of its own holds the message port and the I/O request and reads
  20 ms portions with `CMD_READ`, each continuing at the `io_Offset` of the
  one before, so AHI records without gaps.
- `devices/vhi/vhi_camera.c` grabs from the VHI libraries that
  `uvc.class` publishes for USB cameras (`uvc0.vhi` and on). It picks the
  uncompressed (YUY2) mode nearest to the size the stream asks for and
  takes each picture as RGB24, converted by `uvc.class`. Each library
  present at start becomes a camera `VHI: <card name> (uvc<n>)`.
- `devices/files/` plays raw files, which `aos-phone` offers as test
  options.

The microphone and the loudspeaker run on clocks of their own, and the
MSTicker of the stream on the system clock. The writer of the AHI card
therefore drives the ticker from the number of samples AHI has played
(`ms_ticker_synchronizer_update()`), so the stream neither starves the
card nor piles up sound in front of it.

A source delivers whatever format it has. mediastreamer2 resamples the
captured sound for the codec, and the camera converts each picture from
RGB24 to YUV420P with libyuv in its own thread. The camera does that
conversion itself because mediastreamer2 built with libyuv reads its
RGB24 formats unlike their names: `MS_RGB24` as blue, green, red into
full-range YUV, and `MS_RGB24_REV` as red, green, blue stored from the
bottom row up.

## Layout

| Path | Contents |
|---|---|
| `compat/` | compatibility layer: `libaosnet` and `libaoscompat` |
| `tests/` | tests of the layer, run on AROS |
| `cmake/` | CMake toolchain file and platform description for AROS x86_64 |
| `third_party/linphone-sdk` | upstream linphone-sdk, pinned as a submodule |
| `patches/` | source changes applied to the submodule, see [patches/README.md](patches/README.md) |
| `deps/` | ports of the libraries the SDK needs: jsoncpp, speex, libxml2, mbedtls, libsrtp, opus, libvpx, libyuv, libjpeg-turbo |
| `sdk/` | SDK build settings and documentation |
| `scripts/` | `build-sdk.sh`, which builds and installs everything above, `package.sh`, which packs the programs for AROS, and `build-release.sh`, which does both and builds the programs in between |
| `devices/` | devices for mediastreamer2: the AHI sound card, the camera, the video display, the Mire test camera, and the sources: the AHI microphone, the VHI cameras and raw files |
| `apps/aos-register/` | SIP registration test program, see [its README](apps/aos-register/README.md) |
| `apps/aos-phone/` | the Zune SIP client, see [its README](apps/aos-phone/README.md) |
| `tools/` | host tools for the tests: `sip-registrar` |
| `.githooks/` | git hooks, see [CONTRIBUTING.md](CONTRIBUTING.md) |
| `.github/workflows/` | the GitHub Actions workflow that builds and packs the programs, see [Packing](#packing) |

## Building the compatibility layer

You need the AROS x86_64 cross toolchain built by the AROS `configure`
(crosstools), which also provides the AROS SDK as its sysroot, plus CMake
3.16 or later.

```sh
cmake -S . -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=cmake/aros-x86_64.cmake \
      -DAROS_TOOLCHAIN_DIR=/path/to/toolchain
cmake --build build
```

`AROS_TOOLCHAIN_DIR` is the directory that holds `x86_64-aros-gcc`. The
build produces `compat/libaosnet.a`, `compat/libaoscompat.a` and the test
programs. Warnings are errors in this project's code; `-DAOS_WERROR=OFF`
turns that off.

The whole SDK, with the layer, the libraries from `deps/` and the patched
linphone stack, is built by `scripts/build-sdk.sh`; see
[sdk/README.md](sdk/README.md).

## Running the tests

The tests are AROS programs. CTest runs them through a command you supply
that copies a program to a running AROS system, starts it and returns its
exit code:

```sh
cmake -B build -DAOS_TEST_RUNNER="/path/to/runner;--timeout;120"
ctest --test-dir build --output-on-failure
```

The runner receives the program path followed by its arguments. Each test
prints one `PASS` or `FAIL` line per check and returns 0 when all of them
pass. Lines starting with `info:` report measurements and properties of the
AROS network stack that the tests do not judge.

The network tests need AROSTCP running with a configured interface.
`test_net_resolve` also expects at least one DNS server from the network
configuration. `test_dns` (in `apps/aos-phone/tests`) resolves real names,
`sip.linphone.org` by default, with `getaddrinfo()` and with the resolver of
belle-sip (A and SRV records), so its DNS server has to reach the internet.
`test_https` fetches `https://subscribe.linphone.org/api/ping` through
belle-sip's HTTP client, the one liblinphone's account services use, and
checks the certificate against `share/linphone/rootca.pem`, which has to lie
next to the program, as the belr grammars do.
`test_hw_sources` records from AHI unit 0 and grabs from a USB camera of
`uvc.class`; without such a camera it skips the camera part. No other
program may use the devices meanwhile.

## Packing

`scripts/package.sh` packs a build of `aos-phone` (and, with `-r`,
`aos-register`) into `aos-linphone-<version>-aros-x86_64.lha` and `.zip` in
`builds/`, which git ignores. Each archive holds the drawer `aos-linphone`
with the programs, their icons, the files they read from `share/`, the
documentation and `SOURCE.txt`, which names the commits of the source as
the AGPL asks, plus the drawer icon `aos-linphone.info` next to it. The LHA
archive needs an lha archiver (LHa for UNIX or jlha-utils); `--no-lha` makes
the ZIP archive alone.
The script header lists the options.

The version lives in `cmake/version.cmake`: version.revision, the form of
AmigaOS version strings, and the date of the release. The programs carry it
in their `$VER:` string, which the `Version` command shows. Archive names
follow the release tags: `0.1` on the commit tagged `v0.1`,
`0.1-3-g1234abc` three commits later. A release sets both lines in that
file, commits, and tags the commit:

```
git tag -a v0.1 -m "aos-linphone 0.1"
git push origin v0.1
```

`package.sh` stops when a commit tagged `v*` carries another version, so a
tag cannot publish archives with a different version inside.

`scripts/build-release.sh <toolchain> <work dir>` does the whole chain:
the SDK (skipped when the prefix in the work directory already holds it),
both programs, then `package.sh`.

The workflow `.github/workflows/package.yml` runs that chain on GitHub, on
an Ubuntu 24.04 runner, and keeps the archives as an artifact of the run; a
tag `v*` also publishes them as a GitHub release. The port needs the AROS
toolchain and SDK of one AROS revision (older SDKs lack parts of
bsdsocket.library that the compatibility layer uses), so the first run
builds them from the AROS sources, as the AROS project's own CI does, and
caches them under `/opt/aros`, the path the toolchain records. That first
run takes about 40 minutes, most of it the AROS build; later runs restore
the toolchain, the SDK and the built linphone SDK from the cache.

## Using the compatibility layer

Compile every source that calls socket functions, which means the whole
linphone stack, with the layer's header forced in first, and link both
archives:

```sh
-I<prefix>/include -include aos_compat.h ... -laosnet -laoscompat
```

CMake targets that link `aosnet` get both options automatically. With a
hand-written link line, put both archives after the libraries that use
them: the AROS C library supplies weak placeholder `select()` and `pipe()`
functions, which take over silently when the linker meets a reference to
them only after `libaosnet`. Do not link the SDK's `libnet.a`, which
defines its own `select()` and `inet_ntoa()`.

### libaosnet

`aos_compat.h` replaces the bsdsocket.library prototypes and macros of the
AROS SDK with plain POSIX declarations (`socket()`, `sendto()`,
`getaddrinfo()`, `select()` and the rest) that C and C++ code reach under
the same names. The header also:

- sets `FD_SETSIZE` to 1024, because AROS descriptor numbers are
  system-wide and can be larger than the default of 64;
- gives C sources the same feature environment that g++ uses (POSIX.1-2008,
  XSI and BSD), whatever the include order;
- keeps the cancellation-point macros of the AROS `<pthread.h>` out, so C++
  methods named `send()`, `connect()` or `select()` compile.

Behaviour of the library:

- bsdsocket.library is opened once, before `main()` and before C++ static
  constructors, and shared by all threads. Each thread sees its own `errno`.
  When AROSTCP is not running the program still starts, every network call
  fails with `ENETDOWN`, and `select()` or `poll()` on no descriptors still
  sleep for the timeout. `pipe()` fails too, so a linphone core cannot be
  created: check `aos_net_status()` first and tell the user.
- `close()`, `read()`, `write()`, `fcntl()` and `ioctl()` on sockets stay
  with the AROS C library, which hands them to the network stack.
- `select()` and `poll()` wait on sockets only. `poll()` reports any other
  descriptor as `POLLNVAL`, and reports end of stream and socket errors as
  readiness, as `select()` does.
- `pipe()` returns a connected pair of loopback TCP sockets, because only
  sockets can be waited on. It keeps stream semantics, including end of
  stream when the write end is closed. The replacement applies to the whole
  program; it lives in its own object file, linked only when the program
  calls `pipe()`.
- `getaddrinfo()` supports `AI_V4MAPPED` and `AI_ALL`, which the AROS stack
  lacks. Results are copied, so `freeaddrinfo()` may run in any thread.
- `in6addr_any` and `in6addr_loopback` are defined here; the AROS SDK has
  them only in its `net.lib`, which this library replaces.
- `aos_net_dns_servers()` lists the DNS servers configured in AROSTCP, for
  `linphone_core_set_dns_servers()`: the linphone resolver otherwise looks
  for `/etc/resolv.conf`.
- The program must not define its own `SocketBase`; the library provides it.

### libaoscompat

- `iconv()` converts between UTF-8, US-ASCII, ISO-8859-1 and the 8-bit
  character sets of codesets.library. The name `unknown`, which
  `nl_langinfo(CODESET)` returns on AROS, means the system code page.
  Other multi-byte encodings such as UTF-16 are refused.
- `dlopen()` and `dlsym()` always fail and `dlerror()` says why: programs are
  linked statically and load no modules at run time. The same functions are
  also installed as `libdl.a`, so that build systems testing `-ldl` find
  them.
- `get_current_dir_name()` and `getpagesize()`, which the AROS C library
  lacks. `aos_compat.h` also declares `srandom()`, which the AROS C library
  implements but declares only with `_GNU_SOURCE`.
- Entropy, time and timers for mbedtls built with the hooks in
  `deps/mbedtls/aros_user_config.h`: `mbedtls_hardware_poll()` reads
  `getentropy()`, and `mbedtls_ms_time()` and the `mbedtls_timing_*()`
  functions use `CLOCK_MONOTONIC`.
- `backtrace()` reports no frames.
- `setpriority()` refuses with `EPERM`. Threads change their Exec priority
  with `pthread_setschedparam()` (range -2 to 2 on AROS). The rest of
  `<sys/resource.h>` comes from the AROS C library.

## Known limitations of AROS

- An IPv6 socket without `IPV6_V6ONLY` receives IPv4 traffic as
  IPv4-mapped addresses, but `sendto()` to such an address fails with
  `EADDRNOTAVAIL`. liblinphone therefore has to run with IPv6 disabled
  (`use_ipv6=0`).
- The AROS `e1000.device` network driver allocates and frees memory in
  interrupt context. With network traffic and allocations in tasks at the
  same time (copying test programs to `RAM:` over the network does it), the
  system heap gets corrupted and AROS fails after a while. Restart AROS
  between longer test series on machines with an Intel PRO/1000 adapter,
  including QEMU's `e1000` model.
- At program exit, the pthread library waits for every thread to end. A
  thread that never ends keeps the program alive without a window until
  AROS restarts; on Linux, `exit()` would kill it. liblinphone left such a
  thread behind after calls whose video never started, which
  `patches/linphone-sdk/0008-liblinphone-ticker-of-prepared-stream.patch`
  fixes.
- `usbaudio.class` makes AHI modes only for USB audio devices that play.
  A device that only records, such as the microphone of a web camera, gets
  no mode, so AHI cannot record from it.
- The initial task of a program has the pthread id 0, which belle-sip and
  liblinphone take to mean "no thread": belle-sip then never frees its
  pools of unowned objects, and memory grows with SIP traffic. Run the
  linphone core in a thread started with `pthread_create()`, which gets an
  id of 1 or more, with a stack of about 1 MiB; `aos-register` and
  `aos-phone` do so.

