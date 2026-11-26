# Sourced by scripts/build-sdk.sh and scripts/build-release.sh: tells the
# target system from a cross toolchain directory.
#
#   aos_toolchain_args <repo> <toolchain dir>
#
# sets
#   system     aros (the directory holds x86_64-aros-gcc) or morphos (it
#              holds bin/ppc-morphos-gcc)
#   tc         the CMake arguments for that toolchain file and directory
#   sdk_cache  sdk/<system>-sdk.cmake, the initial cache of SDK builds
#
# and stops the script with a message for any other directory.

aos_toolchain_args() {
    if [ -x "$2/x86_64-aros-gcc" ]; then
        system=aros
        tc=(-DCMAKE_TOOLCHAIN_FILE="$1/cmake/aros-x86_64.cmake" -DAROS_TOOLCHAIN_DIR="$2")
    elif [ -x "$2/bin/ppc-morphos-gcc" ]; then
        system=morphos
        tc=(-DCMAKE_TOOLCHAIN_FILE="$1/cmake/morphos-ppc.cmake" -DMORPHOS_TOOLCHAIN_DIR="$2")
    else
        echo "$2 holds neither x86_64-aros-gcc nor bin/ppc-morphos-gcc" >&2
        exit 1
    fi
    sdk_cache="$1/sdk/$system-sdk.cmake"
}
