# CMake toolchain file for MorphOS (PowerPC).
#
# Uses a ppc-morphos cross GCC, for example the one of pkgsrc
# cross/ppc-morphos-gcc, whose directory holds bin/ppc-morphos-gcc and is
# also the MorphOS SDK sysroot:
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/morphos-ppc.cmake \
#         -DMORPHOS_TOOLCHAIN_DIR=<directory with bin/ppc-morphos-gcc>
#
# MORPHOS_TOOLCHAIN_DIR can also come from the environment. Programs use
# the libnix C library, because programs on the default ixemul may not use
# MUI, and libpthread; Platform/MorphOS-GNU-*.cmake put both into every
# compile and link command.

set(CMAKE_SYSTEM_NAME MorphOS)
set(CMAKE_SYSTEM_PROCESSOR ppc)

# Platform/MorphOS.cmake lives next to this file.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

if(NOT MORPHOS_TOOLCHAIN_DIR)
    set(MORPHOS_TOOLCHAIN_DIR "$ENV{MORPHOS_TOOLCHAIN_DIR}")
endif()
if(NOT MORPHOS_TOOLCHAIN_DIR OR NOT EXISTS "${MORPHOS_TOOLCHAIN_DIR}/bin/ppc-morphos-gcc")
    message(FATAL_ERROR
        "MORPHOS_TOOLCHAIN_DIR must name the directory that contains bin/ppc-morphos-gcc")
endif()
# try_compile() projects read the toolchain file again.
set(ENV{MORPHOS_TOOLCHAIN_DIR} "${MORPHOS_TOOLCHAIN_DIR}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    MORPHOS_TOOLCHAIN_DIR CMAKE_C_STANDARD_LIBRARIES CMAKE_CXX_STANDARD_LIBRARIES)

set(_morphos_prefix "${MORPHOS_TOOLCHAIN_DIR}/bin/ppc-morphos-")
set(CMAKE_C_COMPILER "${_morphos_prefix}gcc")
set(CMAKE_CXX_COMPILER "${_morphos_prefix}g++")
set(CMAKE_AR "${_morphos_prefix}ar" CACHE FILEPATH "MorphOS archiver")
set(CMAKE_RANLIB "${_morphos_prefix}ranlib" CACHE FILEPATH "MorphOS ranlib")
set(CMAKE_NM "${_morphos_prefix}nm" CACHE FILEPATH "MorphOS nm")
set(CMAKE_OBJDUMP "${_morphos_prefix}objdump" CACHE FILEPATH "MorphOS objdump")
set(CMAKE_STRIP "${_morphos_prefix}strip" CACHE FILEPATH "MorphOS strip")
set(CMAKE_SYSROOT "${MORPHOS_TOOLCHAIN_DIR}")

# The C library and threads of every compile and link command.
set(MORPHOS_CLIB_FLAGS "-mclib=libnix -pthread")

# Prefixes holding MorphOS libraries built outside the SDK (CMAKE_PREFIX_PATH)
# are roots too, ahead of the SDK.
set(CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH} "${MORPHOS_TOOLCHAIN_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
