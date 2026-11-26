# GCC on MorphOS: the C library and threads in every command, no
# position-independent code.
#
# MORPHOS_CLIB_FLAGS (from the toolchain file: libnix and libpthread) go
# into the compile and link rules themselves, so that projects which set
# their own CMAKE_CXX_FLAGS, as several third-party ports do, keep them, and
# configuration checks see the same C library. MorphOS loads executables as
# relocatable ELF objects and has no shared libraries of this kind, so
# targets that ask for POSITION_INDEPENDENT_CODE get no extra flag.
#
# GCC for MorphOS defaults to -fno-use-cxa-atexit, and then registers no
# destructors of thread_local objects at all. -fuse-cxa-atexit registers
# them through libstdc++; destructors of static objects then go through
# __cxa_atexit(), which libaoscompat provides (compat/src/misc/morphos/
# cxa_atexit.c), so every C++ program needs libaoscompat.
set(CMAKE_CXX_COMPILE_OPTIONS_PIC "")
set(CMAKE_CXX_COMPILE_OPTIONS_PIE "")
set(CMAKE_CXX_COMPILE_OBJECT
    "<CMAKE_CXX_COMPILER> ${MORPHOS_CLIB_FLAGS} -fuse-cxa-atexit <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
set(CMAKE_CXX_LINK_EXECUTABLE
    "<CMAKE_CXX_COMPILER> ${MORPHOS_CLIB_FLAGS} <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
