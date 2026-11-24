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
# The configuration is the minimal one: SIP signalling, plain chat and audio
# with G.711, G.722, L16 and Speex; no video, no encryption of media, no
# database, no tools or tests.

if(NOT AOS_PREFIX)
    message(FATAL_ERROR "pass -DAOS_PREFIX=<prefix> before -C aros-sdk.cmake")
endif()

set(LINPHONESDK_PLATFORM "Desktop" CACHE STRING "")
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "")
set(CMAKE_INSTALL_PREFIX "${AOS_PREFIX}" CACHE PATH "")
set(CMAKE_PREFIX_PATH "${AOS_PREFIX}" CACHE PATH "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

# Every unit of the stack sees the POSIX view of the AROS network API, and
# every link (including CMake's configuration checks) gets the compatibility
# libraries.
set(_aos_cflags "-I${AOS_PREFIX}/include -include ${AOS_PREFIX}/include/aos_compat.h")
set(CMAKE_C_FLAGS "${_aos_cflags}" CACHE STRING "")
set(CMAKE_CXX_FLAGS "${_aos_cflags}" CACHE STRING "")
set(_aos_libs "-L${AOS_PREFIX}/lib -laosnet -laoscompat -lpthread")
set(CMAKE_C_STANDARD_LIBRARIES "${_aos_libs}" CACHE STRING "")
set(CMAKE_CXX_STANDARD_LIBRARIES "${_aos_libs}" CACHE STRING "")

# iconv() comes from libaoscompat.
set(Iconv_IS_BUILT_IN FALSE CACHE BOOL "")
set(Iconv_INCLUDE_DIR "${AOS_PREFIX}/include" CACHE PATH "")
set(Iconv_LIBRARY "${AOS_PREFIX}/lib/libaoscompat.a" CACHE FILEPATH "")

# zlib from the AROS SDK, jsoncpp, speex, libxml2 and mbedtls from deps/,
# instead of the SDK building its own submodules. No SQLite.
foreach(lib JSONCPP LIBXML2 SPEEX MBEDTLS ZLIB SQLITE3)
    set(BUILD_${lib} OFF CACHE BOOL "")
endforeach()

set(ENABLE_MBEDTLS ON CACHE BOOL "")
set(ENABLE_OPENSSL OFF CACHE BOOL "")
set(ENABLE_SPEEX ON CACHE BOOL "")
set(ENABLE_XML2 ON CACHE BOOL "")
set(ENABLE_SQLITE OFF CACHE BOOL "")

# No sound backend of mediastreamer2 exists for AROS; the sound card API
# (mssndcard.c) and file playback stay in, and the AROS sound card is
# registered by the application at run time.
set(ENABLE_SOUND OFF CACHE BOOL "")

foreach(opt
        STRICT DOC TOOLS TESTS_COMPONENT UNIT_TESTS ASSETS
        BAUDOT BV16 GSM MKV OPUS WEBRTC_AEC WEBRTC_VAD RNNOISE
        VIDEO ADVANCED_IM XERCESC DAEMON DB_STORAGE FLEXIAPI HIDAPI
        VCARD DECAF LIME LIME_X3DH SRTP ZRTP GOCLEAR PQCRYPTO
        ALSA PULSEAUDIO PORTAUDIO QSA OSS)
    set(ENABLE_${opt} OFF CACHE BOOL "")
endforeach()
