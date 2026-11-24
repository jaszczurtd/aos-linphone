#!/bin/bash
# Builds the linphone SDK for AROS x86_64 and installs it, with everything it
# needs, into one prefix:
#
#   1. the compatibility layer (compat/) -> libaosnet, libaoscompat, libdl
#   2. the third-party ports (deps/)     -> jsoncpp, speex, libxml2, mbedtls
#   3. the patches (patches/apply.sh)    -> applied to third_party/linphone-sdk
#   4. the SDK (third_party/linphone-sdk with sdk/aros-sdk.cmake)
#
# usage: scripts/build-sdk.sh <toolchain dir> <prefix> [<build root>]
#
#   toolchain dir  directory holding x86_64-aros-gcc
#   prefix         install directory, created when missing
#   build root     where build-compat, build-deps and build-sdk go,
#                  default: the repository root
#
# Each step reuses its build directory, so running the script again rebuilds
# only what changed. JOBS sets the parallel job count (default 8). The SDK
# takes its version from `git describe`, which needs the upstream tags; set
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
tc=(-DCMAKE_TOOLCHAIN_FILE="$repo/cmake/aros-x86_64.cmake" -DAROS_TOOLCHAIN_DIR="$toolchain")

if [ ! -x "$toolchain/x86_64-aros-gcc" ]; then
    echo "$toolchain/x86_64-aros-gcc not found" >&2
    exit 1
fi

# The SDK needs only these submodules of its own; mbedtls needs its
# framework submodule to configure.
git -C "$repo" submodule update --init third_party/linphone-sdk
git -C "$sdk" submodule update --init external/jsoncpp external/libxml2 \
    external/speex external/mbedtls
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
cmake -S "$repo" -B "$build/build-compat" -G Ninja "${tc[@]}" -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$build/build-compat" -j "$jobs"
cmake --install "$build/build-compat"

echo "== deps"
cmake -S "$repo/deps" -B "$build/build-deps" -G Ninja "${tc[@]}" -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$build/build-deps" -j "$jobs"

echo "== patches"
"$repo/patches/apply.sh"

echo "== sdk"
cmake -S "$sdk" -B "$build/build-sdk" -G Ninja -DAOS_PREFIX="$prefix" \
    -C "$repo/sdk/aros-sdk.cmake" "${tc[@]}" "${version[@]}"
cmake --build "$build/build-sdk" -j "$jobs"
cmake --install "$build/build-sdk"
