# Initial cache for building linphone-sdk for AROS x86_64 (cmake -C).
#
# Requires AOS_PREFIX: the prefix holding the installed compatibility layer
# (libaosnet, libaoscompat, aos_compat.h) and the dependency ports from
# deps/. The SDK is installed into the same prefix.
#
#   cmake -S third_party/linphone-sdk -B build-sdk -G Ninja \
#         -DAOS_PREFIX=<prefix> -C sdk/aros-sdk.cmake \
#         -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/aros-x86_64.cmake \
#         -DAROS_TOOLCHAIN_DIR=<toolchain>
#
# The settings themselves are in sdk-common.cmake; zlib comes from the AROS
# SDK.

if(NOT AOS_PREFIX)
    message(FATAL_ERROR "pass -DAOS_PREFIX=<prefix> before -C aros-sdk.cmake")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/sdk-common.cmake")
