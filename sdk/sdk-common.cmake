# linphone-sdk settings shared by the AROS and MorphOS builds, included by
# sdk/aros-sdk.cmake and sdk/morphos-sdk.cmake (initial caches, cmake -C).
#
# The configuration is a small one: SIP signalling, plain chat, audio with
# G.711, G.722, L16, Speex and Opus, video with VP8, and SRTP media
# encryption; no ZRTP, no database, no tools or tests.

set(LINPHONESDK_PLATFORM "Desktop" CACHE STRING "")
# Optimised code with function names and line tables (-g1), enough to read
# crash addresses with addr2line. scripts/package.sh packs the programs as
# built into the -debug archives and stripped into the release ones, so both
# run the same code (-g does not change it).
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g1 -DNDEBUG" CACHE STRING "")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g1 -DNDEBUG" CACHE STRING "")
set(CMAKE_INSTALL_PREFIX "${AOS_PREFIX}" CACHE PATH "")
set(CMAKE_PREFIX_PATH "${AOS_PREFIX}" CACHE PATH "")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")

# Every unit of the stack sees the POSIX view of the system's network API,
# and every link (including CMake's configuration checks) gets the
# compatibility libraries. A system file may set CMAKE_<LANG>_FLAGS and
# CMAKE_<LANG>_STANDARD_LIBRARIES before including this one; the first
# value of a cache entry wins.
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

# zlib from the system (or none); jsoncpp, speex, libxml2, mbedtls, libsrtp,
# opus, libvpx, libyuv and libjpeg-turbo from deps/, instead of the SDK
# building its own submodules. No SQLite.
foreach(lib JSONCPP LIBXML2 SPEEX MBEDTLS LIBSRTP2 OPUS LIBVPX LIBYUV LIBJPEGTURBO ZLIB SQLITE3)
    set(BUILD_${lib} OFF CACHE BOOL "")
endforeach()

set(ENABLE_MBEDTLS ON CACHE BOOL "")
set(ENABLE_OPENSSL OFF CACHE BOOL "")
set(ENABLE_SPEEX ON CACHE BOOL "")
set(ENABLE_OPUS ON CACHE BOOL "")
set(ENABLE_XML2 ON CACHE BOOL "")
set(ENABLE_SQLITE OFF CACHE BOOL "")
set(ENABLE_SRTP ON CACHE BOOL "")

# Video: VP8, libyuv for conversion and scaling, libjpeg-turbo for the
# static picture camera and snapshots. The test cameras of mediastreamer2
# (Mire, StaticImage) are built in; the display is registered by the
# application at run time. No OpenGL: mediastreamer2 would find the AROS
# SDK's OpenGL and then require GLEW.
set(ENABLE_VIDEO ON CACHE BOOL "")
set(ENABLE_VPX ON CACHE BOOL "")
set(ENABLE_LIBYUV ON CACHE BOOL "")
set(ENABLE_JPEG ON CACHE BOOL "")
# libjpeg-turbo installs turbojpeg.h into include/turbojpeg, where the
# FindTurboJpeg module of mediastreamer2 does not look.
set(_TurboJpeg_INCLUDE_DIRS "${AOS_PREFIX}/include/turbojpeg" CACHE PATH "")

# No sound backend of mediastreamer2 exists for AROS or MorphOS; the sound
# card API (mssndcard.c) and file playback stay in, and the sound card is
# registered by the application at run time.
set(ENABLE_SOUND OFF CACHE BOOL "")

foreach(opt
        STRICT DOC TOOLS TESTS_COMPONENT UNIT_TESTS ASSETS
        BAUDOT BV16 GSM MKV WEBRTC_AEC WEBRTC_VAD RNNOISE
        ADVANCED_IM XERCESC DAEMON DB_STORAGE FLEXIAPI HIDAPI
        VCARD DECAF LIME LIME_X3DH ZRTP GOCLEAR PQCRYPTO
        ALSA PULSEAUDIO PORTAUDIO QSA OSS
        AV1 QRCODE FFMPEG THEORA OPENH264 V4L GL GLX X11 XV QT_GL SCREENSHARING)
    set(ENABLE_${opt} OFF CACHE BOOL "")
endforeach()
