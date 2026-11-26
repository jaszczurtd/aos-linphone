# Initial cache for building linphone-sdk for MorphOS (cmake -C).
#
# Requires AOS_PREFIX: the prefix holding the installed compatibility layer
# (libaosnet, libaoscompat, aos_compat.h) and the dependency ports from
# deps/. The SDK is installed into the same prefix.
#
#   cmake -S third_party/linphone-sdk -B build-sdk -G Ninja \
#         -DAOS_PREFIX=<prefix> -C sdk/morphos-sdk.cmake \
#         -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/morphos-ppc.cmake \
#         -DMORPHOS_TOOLCHAIN_DIR=<toolchain>
#
# The settings themselves are in sdk-common.cmake. On top of them:
#
#  - every link wraps close(), read(), write() and fcntl() of libnix, so
#    that they reach the socket numbers of libaosnet;
#  - belle-sip looks up A and AAAA records also with getaddrinfo() in a
#    thread (USE_GETADDRINFO_FALLBACK), because the MorphOS network stack
#    does not report the DNS servers it got from DHCP, which belle-sip's own
#    resolver needs;
#  - no zlib: the SDK uses it only when found, and MorphOS has it as a
#    shared library only;
#  - libm is linked as -lm.

if(NOT AOS_PREFIX)
    message(FATAL_ERROR "pass -DAOS_PREFIX=<prefix> before -C morphos-sdk.cmake")
endif()

set(_aos_cflags "-I${AOS_PREFIX}/include -include ${AOS_PREFIX}/include/aos_compat.h -DUSE_GETADDRINFO_FALLBACK")
set(CMAKE_C_FLAGS "${_aos_cflags}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${_aos_cflags}" CACHE STRING "")
set(_aos_libs "-L${AOS_PREFIX}/lib -laosnet -laoscompat -lpthread")
string(APPEND _aos_libs " -Wl,--wrap=close,--wrap=read,--wrap=write,--wrap=fcntl,-u,__wrap_close,-u,__cxa_atexit")
set(CMAKE_C_STANDARD_LIBRARIES "${_aos_libs}" CACHE STRING "")
set(CMAKE_CXX_STANDARD_LIBRARIES "${_aos_libs}" CACHE STRING "")
set(CMAKE_DISABLE_FIND_PACKAGE_ZLIB ON CACHE BOOL "")
# FindOpus of mediastreamer2 looks for libm with find_library(), which does
# not search the library directories of the compiler driver; the driver
# finds the right libm for "-lm" itself.
set(_m_LIBRARY "m" CACHE STRING "")

include("${CMAKE_CURRENT_LIST_DIR}/sdk-common.cmake")
