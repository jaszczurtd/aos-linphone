# CMake toolchain file for AROS x86_64.
#
# Uses the cross compiler built by the AROS configure (crosstools). The
# compiler already knows the AROS SDK as its sysroot; this file only points
# CMake at it.
#
#   cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/aros-x86_64.cmake \
#         -DAROS_TOOLCHAIN_DIR=<directory with x86_64-aros-gcc>
#
# AROS_TOOLCHAIN_DIR can also come from the environment.

set(CMAKE_SYSTEM_NAME AROS)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Platform/AROS.cmake lives next to this file.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

if(NOT AROS_TOOLCHAIN_DIR)
    set(AROS_TOOLCHAIN_DIR "$ENV{AROS_TOOLCHAIN_DIR}")
endif()
if(NOT AROS_TOOLCHAIN_DIR OR NOT EXISTS "${AROS_TOOLCHAIN_DIR}/x86_64-aros-gcc")
    message(FATAL_ERROR
        "AROS_TOOLCHAIN_DIR must name the directory that contains x86_64-aros-gcc")
endif()
# try_compile() projects read the toolchain file again. They also get the
# libraries every link needs (the compatibility layer), so that
# configuration checks see the functions it provides.
set(ENV{AROS_TOOLCHAIN_DIR} "${AROS_TOOLCHAIN_DIR}")
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    AROS_TOOLCHAIN_DIR CMAKE_C_STANDARD_LIBRARIES CMAKE_CXX_STANDARD_LIBRARIES)

set(_aros_prefix "${AROS_TOOLCHAIN_DIR}/x86_64-aros-")
set(CMAKE_C_COMPILER "${_aros_prefix}gcc")
set(CMAKE_CXX_COMPILER "${_aros_prefix}g++")
set(CMAKE_AR "${_aros_prefix}ar" CACHE FILEPATH "AROS archiver")
set(CMAKE_RANLIB "${_aros_prefix}ranlib" CACHE FILEPATH "AROS ranlib")
set(CMAKE_NM "${_aros_prefix}nm" CACHE FILEPATH "AROS nm")
set(CMAKE_OBJDUMP "${_aros_prefix}objdump" CACHE FILEPATH "AROS objdump")
set(CMAKE_STRIP "${_aros_prefix}strip" CACHE FILEPATH "AROS strip")

execute_process(
    COMMAND "${CMAKE_C_COMPILER}" -print-sysroot
    OUTPUT_VARIABLE AROS_SDK_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT AROS_SDK_DIR OR NOT EXISTS "${AROS_SDK_DIR}/include/exec/types.h")
    message(FATAL_ERROR "${CMAKE_C_COMPILER} does not report a usable AROS SDK sysroot")
endif()

# Prefixes holding AROS libraries built outside the SDK (CMAKE_PREFIX_PATH)
# are roots too, ahead of the SDK: otherwise the ONLY modes below would look
# for them inside the SDK, and a library both provide (mbedtls) would come
# from the SDK.
set(CMAKE_FIND_ROOT_PATH ${CMAKE_PREFIX_PATH} "${AROS_SDK_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
