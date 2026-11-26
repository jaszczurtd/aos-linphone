#!/bin/bash
# Builds the linphone SDK for AROS x86_64 or MorphOS and installs it, with
# everything it needs, into one prefix:
#
#   1. the compatibility layer (compat/) -> libaosnet, libaoscompat, libdl
#   2. the third-party ports (deps/)     -> jsoncpp, speex, libxml2, mbedtls, srtp, opus,
#                                           libvpx, libyuv, libjpeg-turbo
#   3. the patches (patches/apply.sh)    -> applied to third_party/linphone-sdk
#   4. the SDK (third_party/linphone-sdk with sdk/<system>-sdk.cmake)
#
# usage: scripts/build-sdk.sh <toolchain dir> <prefix> [<build root>]
#
#   toolchain dir  directory holding x86_64-aros-gcc (AROS), or holding
#                  bin/ppc-morphos-gcc (MorphOS); the system follows from it
#   prefix         install directory, created when missing
#   build root     where build-compat, build-deps and build-sdk go,
#                  default: the repository root
#
# Each step reuses its build directory, so running the script again rebuilds
# only what changed. When the SDK settings (sdk/<system>-sdk.cmake,
# sdk/sdk-common.cmake), the prefix or the toolchain differ from the last
# run, build-sdk is configured afresh, because CMake keeps the entries of an
# existing cache over those of the initial cache.
# JOBS sets the parallel job count (default 8). The SDK configuration needs
# yasm on the build host for both systems (it asks for it whenever VP8 is
# on); for AROS, libjpeg-turbo also needs nasm. The SDK takes its version
# from `git describe`, which needs the upstream tags; set
# LINPHONESDK_VERSION (for example 5.5.41) when the submodule has none.

set -euo pipefail

if [ $# -lt 2 ]; then
    echo "usage: $0 <toolchain dir> <prefix> [<build root>]" >&2
    exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
toolchain=$(cd "$1" && pwd)
mkdir -p "$2"
prefix=$(cd "$2" && pwd)
build=${3:-$repo}
jobs=${JOBS:-8}
sdk="$repo/third_party/linphone-sdk"

# shellcheck source=toolchain-args.sh
. "$repo/scripts/toolchain-args.sh"
aos_toolchain_args "$repo" "$toolchain"

# The SDK needs only these submodules of its own; mbedtls needs its
# framework submodule to configure.
git -C "$repo" submodule update --init third_party/linphone-sdk
git -C "$sdk" submodule update --init external/jsoncpp external/libxml2 \
    external/speex external/mbedtls external/srtp external/opus \
    external/libvpx external/libyuv external/libjpeg-turbo
git -C "$sdk/external/mbedtls" submodule update --init framework

version=()
if [ -n "${LINPHONESDK_VERSION:-}" ]; then
    version=(-DLINPHONESDK_VERSION="$LINPHONESDK_VERSION")
elif ! git -C "$sdk" describe >/dev/null 2>&1; then
    echo "third_party/linphone-sdk has no tags for its version; fetch them" >&2
    echo "(git -C third_party/linphone-sdk fetch --tags) or set LINPHONESDK_VERSION" >&2
    exit 1
fi

echo "== compat"
# Debug information like the SDK (sdk/sdk-common.cmake); package.sh strips it
# from the release archives.
cmake -S "$repo" -B "$build/build-compat" -G Ninja "${tc[@]}" -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_C_FLAGS=-g1 -DCMAKE_CXX_FLAGS=-g1
cmake --build "$build/build-compat" -j "$jobs"
cmake --install "$build/build-compat"

echo "== deps"
cmake -S "$repo/deps" -B "$build/build-deps" -G Ninja "${tc[@]}" -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DAOS_JOBS="$jobs"
cmake --build "$build/build-deps" -j "$jobs"

echo "== patches"
"$repo/patches/apply.sh"

echo "== sdk"
# The initial cache only seeds a new CMakeCache.txt. When its contents, the
# prefix (which it turns into CMAKE_INSTALL_PREFIX) or the toolchain changed
# since build-sdk was last configured, configure it afresh, as
# `cmake --fresh` does: the cache and the top CMakeFiles directory go, the
# compiled objects stay, and the build then compiles only what the new
# settings change.
sdk_build="${build:?}/build-sdk"
settings="$(cat "$sdk_cache" "$repo/sdk/sdk-common.cmake" | sha256sum)
prefix=$prefix
toolchain=$toolchain"
stamp="$sdk_build/aos-sdk-settings.txt"
if [ -f "$sdk_build/CMakeCache.txt" ] && [ "$(cat "$stamp" 2>/dev/null)" != "$settings" ]; then
    echo "-- the SDK settings changed: configuring build-sdk afresh"
    rm -rf "$sdk_build/CMakeCache.txt" "$sdk_build/CMakeFiles"
fi
cmake -S "$sdk" -B "$sdk_build" -G Ninja -DAOS_PREFIX="$prefix" \
    -C "$sdk_cache" "${tc[@]}" "${version[@]}"
printf '%s\n' "$settings" > "$stamp"
cmake --build "$sdk_build" -j "$jobs"
cmake --install "$sdk_build"
