#!/bin/bash
# Builds everything a release needs and packs it: the SDK
# (scripts/build-sdk.sh), aos-phone and aos-register, then the archives of
# scripts/package.sh.
#
# usage: scripts/build-release.sh <toolchain dir> <work dir>
#
#   toolchain dir  directory holding x86_64-aros-gcc
#   work dir       where the SDK prefix (prefix/), its build directories
#                  and the program builds (aos-phone/, aos-register/) go
#
# The SDK step is skipped when <work dir>/prefix already holds liblinphone,
# e.g. restored from a CI cache. ILBMTOICON names the AROS host tool that
# makes the icons (else ilbmtoicon from the PATH). OUT sets where the
# archives go (default: builds/ in the repository); NO_LHA=1 makes the ZIP
# archive alone. JOBS and LINPHONESDK_VERSION go to build-sdk.sh.

set -euo pipefail

if [ $# -ne 2 ]; then
    echo "usage: $0 <toolchain dir> <work dir>" >&2
    exit 2
fi

repo=$(cd "$(dirname "$0")/.." && pwd)
toolchain=$(cd "$1" && pwd)
mkdir -p "$2"
work=$(cd "$2" && pwd)
prefix="$work/prefix"

if [ -f "$prefix/lib/liblinphone.a" ]; then
    echo "== SDK: using $prefix"
else
    "$repo/scripts/build-sdk.sh" "$toolchain" "$prefix" "$work"
fi

icon=()
if [ -n "${ILBMTOICON:-}" ]; then
    icon=(-DAOS_ILBMTOICON="$ILBMTOICON")
fi
for app in aos-phone aos-register; do
    echo "== $app"
    # FindTurboJpeg of the SDK does not look in include/turbojpeg, where the
    # SDK's libjpeg-turbo puts its header.
    # aros-sdk.cmake needs AOS_PREFIX given before it.
    cmake -S "$repo/apps/$app" -B "$work/$app" -G Ninja \
        -DAOS_PREFIX="$prefix" -C "$repo/sdk/aros-sdk.cmake" \
        -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/aros-x86_64.cmake" \
        -DAROS_TOOLCHAIN_DIR="$toolchain" \
        -D_TurboJpeg_INCLUDE_DIRS="$prefix/include/turbojpeg" \
        -D_TurboJpeg_LIBRARY="$prefix/lib/libturbojpeg.a" \
        "${icon[@]}"
    cmake --build "$work/$app"
done

pack=(-o "${OUT:-$repo/builds}" -r "$work/aos-register/aos-register")
if [ "${NO_LHA:-0}" = 1 ]; then
    pack+=(--no-lha)
fi
"$repo/scripts/package.sh" "${pack[@]}" "$work/aos-phone" "$prefix"
