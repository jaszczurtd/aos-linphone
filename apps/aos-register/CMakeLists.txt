# aos-register: SIP registration test program for AROS and MorphOS, linked
# against the SDK installed by scripts/build-sdk.sh. Configure it with the
# same prefix and initial cache as the SDK:
#
#   cmake -S apps/aos-register -B build-register -G Ninja \
#         -DAOS_PREFIX=<prefix> -C sdk/aros-sdk.cmake \
#         -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/aros-x86_64.cmake \
#         -DAROS_TOOLCHAIN_DIR=<toolchain>
#
# For MorphOS: -C sdk/morphos-sdk.cmake, cmake/morphos-ppc.cmake and
# -DMORPHOS_TOOLCHAIN_DIR=<toolchain>.

cmake_minimum_required(VERSION 3.22)

project(aos-register LANGUAGES C CXX)

# Each SDK package config loads its targets before it looks for its own
# dependencies, so the targets those reference must already exist: find the
# packages in dependency order. Iconv comes from libaoscompat (see
# sdk/sdk-common.cmake), ZLIB from the AROS SDK, LibXml2 and jsoncpp from
# deps/.
set(AOS_SYSTEM_PACKAGES Iconv ZLIB LibXml2)
if(CMAKE_SYSTEM_NAME STREQUAL "MorphOS")
    list(REMOVE_ITEM AOS_SYSTEM_PACKAGES ZLIB) # the MorphOS SDK build has no zlib
endif()
foreach(pkg IN LISTS AOS_SYSTEM_PACKAGES)
    find_package(${pkg} REQUIRED)
endforeach()
find_package(jsoncpp REQUIRED CONFIG)
foreach(pkg BCToolbox Belr BelleSIP Ortp Mediastreamer2 LibLinphone)
    find_package(${pkg} REQUIRED CONFIG)
endforeach()

add_executable(aos-register main.c)
target_compile_options(aos-register PRIVATE -Wall -Wextra -Werror)
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/version.cmake)
target_compile_definitions(aos-register PRIVATE AOS_VERSION="${AOS_VERSION}" AOS_VERSION_DATE="${AOS_VERSION_DATE}")
target_link_libraries(aos-register PRIVATE liblinphone)
# liblinphone is C++: link with g++ so that libstdc++ comes along.
set_target_properties(aos-register PROPERTIES LINKER_LANGUAGE CXX)
