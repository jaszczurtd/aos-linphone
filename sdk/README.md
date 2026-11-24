# linphone SDK for AROS

The linphone stack (bctoolbox, belr, belle-sip, ortp, mediastreamer2 and
liblinphone) comes from the upstream `linphone-sdk` monorepo, pinned as the
submodule `third_party/linphone-sdk`. It is built for AROS x86_64 as static
libraries and installed, together with everything it needs, into one prefix
that programs then link against.

## Building

```sh
scripts/build-sdk.sh /path/to/toolchain /path/to/prefix
```

The script needs the AROS cross toolchain (the directory that holds
`x86_64-aros-gcc`), CMake 3.22 or later, Ninja, git and bash. It runs four steps, each in its
own build directory under the repository root (or under the directory given
as a third argument):

1. `build-compat`: the compatibility layer, installed into the prefix
   (`libaosnet`, `libaoscompat`, `libdl` and `aos_compat.h`).
2. `build-deps`: the third-party libraries from `deps/`.
3. `patches/apply.sh`: the few source changes the SDK needs, see
   [patches/README.md](../patches/README.md).
4. `build-sdk`: the SDK itself, configured with `sdk/aros-sdk.cmake`.

The script checks out only the submodules the build uses. The SDK takes its
version from `git describe`, so the submodule needs the upstream tags;
without them, set `LINPHONESDK_VERSION` (for example `5.5.41`). Running the
script again rebuilds only what changed. `JOBS` sets the number of parallel
jobs (default 8).

## Configuration

`sdk/aros-sdk.cmake` is the initial cache of the SDK build. It gives every
compiled unit the forced header `aos_compat.h` and every link, including
CMake's configuration checks, `-laosnet -laoscompat -lpthread`. The build
is a small one: SIP, plain chat, audio with G.711, G.722, L16, Speex and
Opus, and SRTP media encryption. It has no video, no ZRTP, no database,
and no tools or tests. mediastreamer2 has no AROS sound backend yet; its
sound card API stays in, so an AROS card can be registered at run time.

The libraries the SDK would build from its own submodules come from
`deps/` instead: jsoncpp, speex/speexdsp, libxml2, mbedtls 3.6.8, libsrtp,
which uses mbedtls for its cryptography, and opus. The AROS SDK's mbedtls
is a minimal TLS client without thread support that lacks about a third of
the functions bctoolbox uses. The port in `deps/` adds
`deps/mbedtls/aros_user_config.h`, which takes entropy, time and timers
from `libaoscompat`. zlib comes from the AROS SDK.

AROS relocates programs when it loads them and has no shared libraries. Its
loader refuses GOT relocations, which position-independent code needs, with
"file is not executable". `cmake/Platform/AROS-GNU-C.cmake` and
`AROS-GNU-CXX.cmake` therefore clear CMake's PIC flags, which several SDK
libraries ask for.

## Linking a program

A CMake project configured with the same toolchain file and initial cache
finds the installed packages:

```sh
cmake -S <project> -B <build> -G Ninja -DAOS_PREFIX=<prefix> \
      -C sdk/aros-sdk.cmake \
      -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/aros-x86_64.cmake \
      -DAROS_TOOLCHAIN_DIR=<toolchain>
```

Each SDK package config loads its targets before it looks for its own
dependencies, so the program has to find the packages in dependency order,
as [apps/aos-register/CMakeLists.txt](../apps/aos-register/CMakeLists.txt)
does: Iconv, ZLIB, LibXml2 and jsoncpp first, then BCToolbox, Belr,
BelleSIP, Ortp, Mediastreamer2 and LibLinphone. Link with the C++ driver.

At run time the SIP, SDP and address parsers load their grammars from
`belr/grammars/*.belr` under the factory's resource directory. Ship the files
from `<prefix>/share/belr/grammars` with the program and point
`linphone_factory_set_top_resources_dir()` at their parent, for example
`PROGDIR:share`. Without them the first address parsed stops the program.

A change in the compatibility layer's sources leaves the SDK archives
alone: the SDK build compiles nothing again, and programs only need a new
link. A change in its headers, `aos_compat.h` above all, recompiles the
whole SDK.
