# Ports of the third-party libraries the linphone stack needs on AROS and
# that the AROS SDK does not provide in a usable form: jsoncpp,
# speex/speexdsp, libxml2, mbedtls, libsrtp (SRTP media encryption, with
# mbedtls as its crypto engine), opus, and for video libvpx (VP8), libyuv
# (pixel format conversion and scaling) and libjpeg-turbo (the static
# picture camera and JPEG snapshots). zlib comes from the AROS SDK. The
# SDK's mbedtls is a minimal TLS client without threads and lacks a third of
# the functions bctoolbox uses, so mbedtls is built from linphone-sdk's
# submodule with its own configuration plus deps/mbedtls/aros_user_config.h.
#
# The sources are the submodules pinned by linphone-sdk (external/), built
# as separate static libraries with the AROS toolchain and installed into
# CMAKE_INSTALL_PREFIX, where the SDK build then finds them (it runs with
# BUILD_JSONCPP, BUILD_SPEEX, BUILD_LIBXML2, BUILD_MBEDTLS, BUILD_LIBSRTP2,
# BUILD_OPUS, BUILD_LIBVPX, BUILD_LIBYUV and BUILD_LIBJPEGTURBO off). Each
# library gets the options the SDK itself would give it, without
# position-independent code (see cmake/Platform/AROS-GNU-C.cmake). mbedtls,
# libsrtp and libjpeg-turbo also take headers from the compatibility layer,
# so install that into the same prefix first.
#
# libvpx and libjpeg-turbo contain x86_64 assembly: yasm and nasm must be
# installed on the build host.
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

foreach(lib jsoncpp speex libxml2 mbedtls srtp opus libvpx libyuv libjpeg-turbo)
    # libvpx has a configure script instead of CMake.
    if(NOT EXISTS "${AOS_SDK_SOURCE_DIR}/external/${lib}/CMakeLists.txt" AND
       NOT EXISTS "${AOS_SDK_SOURCE_DIR}/external/${lib}/configure")
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

# libsrtp with mbedtls as its crypto engine, as the SDK builds it
# (cmake/ExternalDependencies.cmake), with the same forced header as mbedtls.
ExternalProject_Add(srtp
    SOURCE_DIR "${AOS_SDK_SOURCE_DIR}/external/srtp"
    DEPENDS mbedtls
    CMAKE_ARGS ${AOS_PORT_ARGS}
        -DENABLE_MBEDTLS=ON
        -DENABLE_OPENSSL=OFF
        -DLIBSRTP_TEST_APPS=OFF
        -DENABLE_WARNINGS_AS_ERRORS=OFF
        "-DCMAKE_C_FLAGS=-include ${CMAKE_INSTALL_PREFIX}/include/aos_compat.h")

# The Opus codec, floating point with run-time detection of the x86 vector
# extensions, as the SDK builds it for desktops. The AROS toolchain has no
# stack protector runtime.
ExternalProject_Add(opus
    SOURCE_DIR "${AOS_SDK_SOURCE_DIR}/external/opus"
    CMAKE_ARGS ${AOS_PORT_ARGS}
        -DOPUS_BUILD_PROGRAMS=OFF
        -DOPUS_STACK_PROTECTOR=OFF
        -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF
        -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=ON)

# libvpx with its own configure script and the options the SDK gives it
# (cmake/ExternalDependencies.cmake): VP8 only, real-time, threads, SIMD with
# run-time CPU detection. The x86_64 Linux target with the AROS cross tools
# produces what AROS loads (ELF objects, yasm in elf64 format); configure
# turns PIC on for that target, so it is turned off explicitly.
include(ProcessorCount)
ProcessorCount(_aos_cpus)
if(_aos_cpus EQUAL 0)
    set(_aos_cpus 1)
endif()
set(AOS_JOBS ${_aos_cpus} CACHE STRING "Parallel jobs of the make-based ports (libvpx)")
ExternalProject_Add(libvpx
    SOURCE_DIR "${AOS_SDK_SOURCE_DIR}/external/libvpx"
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env CROSS=${AROS_TOOLCHAIN_DIR}/x86_64-aros-
        <SOURCE_DIR>/configure
        --target=x86_64-linux-gcc
        --prefix=${CMAKE_INSTALL_PREFIX}
        --libdir=${CMAKE_INSTALL_PREFIX}/lib
        --enable-static
        --disable-shared
        --disable-pic
        --enable-error-concealment
        --enable-multithread
        --enable-realtime-only
        --enable-spatial-resampling
        --enable-vp8
        --disable-vp9
        --enable-libs
        --disable-install-docs
        --disable-debug-libs
        --disable-examples
        --disable-unit-tests
        --disable-tools
        --as=yasm
    BUILD_COMMAND make -j${AOS_JOBS}
    INSTALL_COMMAND make install)

# libyuv adds -fPIC to the compiler flags of every non-Windows build; the
# -fno-PIC of the configuration flags comes later on the command line and
# wins. Without the system paths it does not find the AROS SDK's libjpeg:
# MJPEG decoding is not needed, and every program would have to link
# libjpeg.
set(AOS_NO_PIC_FLAGS "-O2 -DNDEBUG -fno-PIC")
ExternalProject_Add(libyuv
    SOURCE_DIR "${AOS_SDK_SOURCE_DIR}/external/libyuv"
    CMAKE_ARGS ${AOS_PORT_ARGS}
        -DCMAKE_FIND_USE_CMAKE_SYSTEM_PATH=OFF
        "-DCMAKE_C_FLAGS_RELEASE=${AOS_NO_PIC_FLAGS}"
        "-DCMAKE_CXX_FLAGS_RELEASE=${AOS_NO_PIC_FLAGS}")

# libjpeg-turbo with the TurboJPEG API that mediastreamer2 uses, and its
# x86_64 SIMD code (nasm). The forced header of the compatibility layer gives
# its C sources the feature environment of the rest of the stack, in which
# <stdlib.h> declares setenv().
ExternalProject_Add(libjpeg-turbo
    SOURCE_DIR "${AOS_SDK_SOURCE_DIR}/external/libjpeg-turbo"
    CMAKE_ARGS ${AOS_PORT_ARGS}
        -DWITH_TURBOJPEG=ON
        -DWITH_JAVA=OFF
        -DENABLE_TOOLS=OFF
        "-DCMAKE_C_FLAGS=-include ${CMAKE_INSTALL_PREFIX}/include/aos_compat.h")
