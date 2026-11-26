# Compatibility layer: the POSIX pieces the linphone stack needs on AROS
# and MorphOS.
#
#   aosnet     sockets, name resolution, select()/poll(), pipe() over
#              bsdsocket.library
#   aoscompat  dlopen() stubs, backtrace() stubs, setpriority(), iconv(),
#              get_current_dir_name(), the mbedtls entropy, clock and timer
#              functions (headers in include/mbedtls/); on MorphOS also
#              nl_langinfo(), strptime(), timezone and the few POSIX
#              functions libnix declares without implementing
#
# The headers are common: aos_compat.h with its system parts in include/aos/.
# The sources are common except for the network backend (src/net/<system>/)
# and the few functions that depend on the system C library or system
# libraries (src/misc/<system>/).
#
# Code that links aosnet must be compiled with aos_compat.h forced in; the
# PUBLIC options below pass that on to every consumer target.

set(AOS_COMPAT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/aos_compat.h")

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(CMAKE_SYSTEM_NAME STREQUAL "AROS")
    set(AOS_NET_SYSTEM_SOURCES
        src/net/aros/net_base.c
        src/net/aros/net_dns.c
        src/net/aros/net_resolve.c
        src/net/aros/net_socket.c
        src/net/aros/net_wait.c)
    set(AOS_MISC_SYSTEM_SOURCES
        src/misc/aros/entropy.c
        src/misc/aros/iconv.c
        src/misc/aros/pagesize.c)
else()
    set(AOS_NET_SYSTEM_SOURCES
        src/net/morphos/net_base.c
        src/net/morphos/net_fd.c
        src/net/morphos/net_ns.c
        src/net/morphos/net_resolve.c
        src/net/morphos/net_socket.c
        src/net/morphos/net_wait.c)
    set(AOS_MISC_SYSTEM_SOURCES
        src/misc/morphos/cxa_atexit.c
        src/misc/morphos/entropy.c
        src/misc/morphos/iconv.c
        src/misc/morphos/langinfo.c
        src/misc/morphos/pagesize.c
        src/misc/morphos/posix.c
        src/misc/morphos/time.c)
endif()

add_library(aosnet STATIC src/net/net_pipe.c ${AOS_NET_SYSTEM_SOURCES})
target_include_directories(aosnet PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# Joined form (-include<file>), so that CMake does not merge a repeated
# "-include" word, and only for C and C++ sources.
target_compile_options(aosnet
    PUBLIC
        "$<$<COMPILE_LANGUAGE:C,CXX>:-include$<BUILD_INTERFACE:${AOS_COMPAT_HEADER}>$<INSTALL_INTERFACE:aos_compat.h>>")
target_link_libraries(aosnet PRIVATE aos_warnings)
if(CMAKE_SYSTEM_NAME STREQUAL "MorphOS")
    # close(), read(), write() and fcntl() of libnix reach socket numbers
    # through the __wrap_ functions of net_fd.c. -u pulls that object in also
    # when only libnix itself calls close() (fclose()), which the linker sees
    # after libaosnet.
    target_link_options(aosnet INTERFACE
        "LINKER:--wrap=close,--wrap=read,--wrap=write,--wrap=fcntl,-u,__wrap_close")
endif()

add_library(aoscompat STATIC
    src/misc/cwd.c
    src/misc/dlfcn.c
    src/misc/mbedtls_alt.c
    src/misc/execinfo.c
    src/misc/resource.c
    ${AOS_MISC_SYSTEM_SOURCES}
)
target_include_directories(aoscompat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(aoscompat PRIVATE aos_warnings PUBLIC pthread)
if(CMAKE_SYSTEM_NAME STREQUAL "MorphOS")
    # C++ objects built with -fuse-cxa-atexit (the MorphOS platform files)
    # and libstdc++, which the linker sees after libaoscompat, call
    # __cxa_atexit() of cxa_atexit.c; -u pulls that object in first.
    target_link_options(aoscompat INTERFACE "LINKER:-u,__cxa_atexit")
endif()

# libdl.a: the same dlopen() stubs under the name build systems look for
# ("-ldl"). With it, liblinphone and mediastreamer2 detect dlopen() and take
# their usual code path, which fails cleanly at run time; their code without
# HAVE_DLOPEN does not compile.
add_library(dl STATIC src/misc/dlfcn.c)
target_include_directories(dl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(dl PRIVATE aos_warnings)

install(TARGETS aosnet aoscompat dl ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
