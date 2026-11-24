# Compatibility layer: the POSIX pieces the linphone stack needs on AROS.
#
#   aosnet     sockets, name resolution, select()/poll(), pipe() over
#              bsdsocket.library
#   aoscompat  dlopen() stubs, backtrace() stubs, setpriority(), iconv(),
#              get_current_dir_name(), the mbedtls entropy, clock and timer
#              functions (headers in include/mbedtls/)
#
# Code that links aosnet must be compiled with aos_compat.h forced in; the
# PUBLIC options below pass that on to every consumer target.

set(AOS_COMPAT_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/include/aos_compat.h")

add_library(aosnet STATIC
    src/net/net_base.c
    src/net/net_dns.c
    src/net/net_pipe.c
    src/net/net_resolve.c
    src/net/net_socket.c
    src/net/net_wait.c
)
target_include_directories(aosnet PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
# Joined form (-include<file>), so that CMake does not merge a repeated
# "-include" word, and only for C and C++ sources.
target_compile_options(aosnet
    PUBLIC
        "$<$<COMPILE_LANGUAGE:C,CXX>:-include$<BUILD_INTERFACE:${AOS_COMPAT_HEADER}>$<INSTALL_INTERFACE:aos_compat.h>>")
target_link_libraries(aosnet PRIVATE aos_warnings)

add_library(aoscompat STATIC
    src/misc/cwd.c
    src/misc/dlfcn.c
    src/misc/entropy.c
    src/misc/mbedtls_alt.c
    src/misc/execinfo.c
    src/misc/iconv.c
    src/misc/pagesize.c
    src/misc/resource.c
)
target_include_directories(aoscompat PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(aoscompat PRIVATE aos_warnings PUBLIC pthread)

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
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
