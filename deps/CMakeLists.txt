# Ports of the third-party libraries the linphone stack needs on AROS and
# that the AROS SDK does not provide in a usable form: jsoncpp,
# speex/speexdsp, libxml2 and mbedtls. zlib comes from the AROS SDK. The
# SDK's mbedtls is a minimal TLS client without threads and lacks a third of
# the functions bctoolbox uses, so mbedtls is built from linphone-sdk's
# submodule with its own configuration plus deps/mbedtls/aros_user_config.h.
#
# The sources are the submodules pinned by linphone-sdk (external/), built
# as separate static libraries with the AROS toolchain and installed into
# CMAKE_INSTALL_PREFIX, where the SDK build then finds them (it runs with
# BUILD_JSONCPP, BUILD_SPEEX, BUILD_LIBXML2 and BUILD_MBEDTLS off). Each
# library gets the options the SDK itself would give it. mbedtls also takes
# headers from the compatibility layer, so install that into the same prefix
# first.
#
#   cmake -S deps -B build-deps -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/aros-x86_64.cmake \
#         -DAROS_TOOLCHAIN_DIR=<toolchain> -DCMAKE_INSTALL_PREFIX=<prefix>
#   cmake --build build-deps

cmake_minimum_required(VERSION 3.16)

project(aos-linphone-deps NONE)

include(ExternalProject)

set(AOS_SDK_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/linphone-sdk"
    CACHE PATH "linphone-sdk source tree whose external/ submodules are built")
get_filename_component(AOS_SDK_SOURCE_DIR "${AOS_SDK_SOURCE_DIR}" ABSOLUTE)

foreach(lib jsoncpp speex libxml2 mbedtls)
    if(NOT EXISTS "${AOS_SDK_SOURCE_DIR}/external/${lib}/CMakeLists.txt")
        message(FATAL_ERROR "${AOS_SDK_SOURCE_DIR}/external/${lib} is not checked out; "
            "run: git -C ${AOS_SDK_SOURCE_DIR} submodule update --init external/${lib}")
    endif()
endforeach()

set(AOS_PORT_ARGS
    -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
    -DAROS_TOOLCHAIN_DIR=${AROS_TOOLCHAIN_DIR}
    -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX}
    -DCMAKE_PREFIX_PATH=${CMAKE_INSTALL_PREFIX}
    -DCMAKE_BUILD_TYPE=Release
    -DBUILD_SHARED_LIBS=OFF)

ExternalProject_Add(jsoncpp
    SOURCE_DIR "${AOS_SDK_SOURCE_DIR}/external/jsoncpp"
    CMAKE_ARGS ${AOS_PORT_ARGS}
        -DBUILD_STATIC_LIBS=ON
        -DBUILD_OBJECT_LIBS=OFF
        -DJSONCPP_WITH_TESTS=OFF
        -DJSONCPP_WITH_POST_BUILD_UNITTEST=OFF
        -DJSONCPP_WITH_PKGCONFIG_SUPPORT=OFF
        -DJSONCPP_WITH_EXAMPLE=OFF)

ExternalProject_Add(speex
    SOURCE_DIR "${AOS_SDK_SOURCE_DIR}/external/speex"
    CMAKE_ARGS ${AOS_PORT_ARGS}
        -DENABLE_SPEEX_CODEC=ON
        -DENABLE_SPEEX_DSP=ON)

ExternalProject_Add(libxml2
    SOURCE_DIR "${AOS_SDK_SOURCE_DIR}/external/libxml2"
    CMAKE_ARGS ${AOS_PORT_ARGS})

ExternalProject_Add(mbedtls
    SOURCE_DIR "${AOS_SDK_SOURCE_DIR}/external/mbedtls"
    CMAKE_ARGS ${AOS_PORT_ARGS}
        -DUSE_STATIC_MBEDTLS_LIBRARY=ON
        -DUSE_SHARED_MBEDTLS_LIBRARY=OFF
        -DENABLE_PROGRAMS=OFF
        -DENABLE_TESTING=OFF
        -DMBEDTLS_FATAL_WARNINGS=OFF
        -DMBEDTLS_USER_CONFIG_FILE=${CMAKE_CURRENT_SOURCE_DIR}/mbedtls/aros_user_config.h
        # timing_alt.h from the installed compatibility layer, and its forced
        # header for the POSIX socket prototypes (x509_crt.c calls
        # inet_pton(), which the bare AROS <arpa/inet.h> does not declare).
        "-DCMAKE_C_FLAGS=-I${CMAKE_INSTALL_PREFIX}/include/mbedtls -include ${CMAKE_INSTALL_PREFIX}/include/aos_compat.h")
