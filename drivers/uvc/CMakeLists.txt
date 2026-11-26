# uvc.class for MorphOS: the AROS Poseidon class for USB video cameras,
# ported to MorphOS. It publishes each camera as a VHI library uvc<unit>.vhi,
# which the camera sources of aos-phone open (devices/vhi). A separate
# project, built for MorphOS only:
#
#   cmake -S drivers/uvc -B build-uvc -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/morphos-ppc.cmake \
#         -DMORPHOS_TOOLCHAIN_DIR=<directory with bin/ppc-morphos-gcc>
#   cmake --build build-uvc
#
# The result, build-uvc/uvc.class, goes to SYS:MorphOS/Classes/USB, where
# AddUSBClasses finds it (see drivers/README.md).
#
# Licensed under the AROS Public License version 1.1, like the AROS class
# it comes from (see LICENCE.md of the repository).

cmake_minimum_required(VERSION 3.16)
project(uvc_class LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "MorphOS")
    message(FATAL_ERROR "drivers/uvc builds uvc.class for MorphOS; use cmake/morphos-ppc.cmake")
endif()

add_executable(uvc_class uvc.class.c uvc_vhi.c)
set_target_properties(uvc_class PROPERTIES OUTPUT_NAME uvc.class)
target_include_directories(uvc_class PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The SDK's inline tag-list macros put pointers into ULONG arrays.
target_compile_options(uvc_class PRIVATE -O2 -g1 -Wall -Wextra -Wno-int-conversion -Wno-stringop-truncation)
# A library: no start-up code; exec finds the class through its Resident.
target_link_options(uvc_class PRIVATE -nostartfiles)
