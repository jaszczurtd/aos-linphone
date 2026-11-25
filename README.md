# aos-linphone

A SIP client for AROS with audio and video calls and chat, built on
[liblinphone](https://gitlab.linphone.org/BC/public/linphone-sdk) and a
Zune user interface.

The work is at an early stage. What exists today:

- the compatibility layer that lets the linphone stack be built for AROS
  without changes for each AROS quirk: a POSIX view of the AROS network API
  and the few system functions the stack needs;
- the build of the linphone SDK for AROS on top of it, see
  [sdk/README.md](sdk/README.md);
- `aos-register`, a console program that registers a SIP account from AROS;
- `aos-phone`, the Zune client: account registration, calls with sound and
  video, and text chat, see [its README](apps/aos-phone/README.md). It
  plays the sound of calls and the ring tone through AHI, shows the picture
  of the other side, plays sound files into calls and records calls. It
  sends the test pictures of mediastreamer2 and does not take sound from a
  microphone yet.

The microphone and the camera come next.

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
| `scripts/` | `build-sdk.sh`, which builds and installs everything above |
| `devices/` | devices for mediastreamer2: the AHI sound card, the video display and the Mire test camera |
| `apps/aos-register/` | SIP registration test program, see [its README](apps/aos-register/README.md) |
| `apps/aos-phone/` | the Zune SIP client, see [its README](apps/aos-phone/README.md) |
| `tools/` | host tools for the tests: `sip-registrar` |
| `.githooks/` | git hooks, see [Committing changes](#committing-changes) |

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
configuration.

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
- The initial task of a program has the pthread id 0, which belle-sip and
  liblinphone take to mean "no thread": belle-sip then never frees its
  pools of unowned objects, and memory grows with SIP traffic. Run the
  linphone core in a thread started with `pthread_create()`, which gets an
  id of 1 or more, with a stack of about 1 MiB; `aos-register` and
  `aos-phone` do so.

## Committing changes

Two git hooks in `.githooks/` keep the code style and the commit messages
consistent. Enable them once in each clone:

```sh
git config core.hooksPath .githooks
```

The code style lives in `.clang-format`: opening braces stay on the line of
the function or statement, indentation is four spaces, and line breaks are
left to the author.

On every commit the pre-commit hook fixes the staged text files and stages
the result. It turns CRLF line ends into LF, removes trailing spaces and
tabs, adds a missing final newline, and replaces typographic quotes, dashes,
arrows, ellipses and no-break spaces with ASCII. C and C++ sources then go
through `clang-format` with that style; when `clang-format` is not
installed, the hook skips that step. Patch
files (`*.patch`, `*.diff`) stay as they are, because their context lines
must match the upstream sources.

The hook works on the staged version of each file, so a partly staged file
commits only its staged part. When nothing is left to commit after these
fixes, the commit stops with an error.

The commit-msg hook accepts messages in the
[Conventional Commits](https://www.conventionalcommits.org/) form
`type(scope): subject`, for example
`feat(compat): add poll() over WaitSelect`. The type is one of `feat`,
`fix`, `docs`, `style`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`
and `revert`; the scope is optional and lower-case, and `!` after the type
or scope marks a breaking change. The subject line takes at most 100 bytes,
which is 100 characters of plain ASCII. Messages that git writes itself
for merges, reverts and `fixup!`, `squash!` or `amend!` commits pass as they
are.
