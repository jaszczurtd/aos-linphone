# GCC on MorphOS: the C library and threads in every command, no
# position-independent code.
#
# MORPHOS_CLIB_FLAGS (from the toolchain file: libnix and libpthread) go
# into the compile and link rules themselves, so that projects which set
# their own CMAKE_C_FLAGS, as several third-party ports do, keep them, and
# configuration checks see the same C library. MorphOS loads executables as
# relocatable ELF objects and has no shared libraries of this kind, so
# targets that ask for POSITION_INDEPENDENT_CODE get no extra flag.
set(CMAKE_C_COMPILE_OPTIONS_PIC "")
set(CMAKE_C_COMPILE_OPTIONS_PIE "")
set(CMAKE_C_COMPILE_OBJECT
    "<CMAKE_C_COMPILER> ${MORPHOS_CLIB_FLAGS} <DEFINES> <INCLUDES> <FLAGS> -o <OBJECT> -c <SOURCE>")
set(CMAKE_C_LINK_EXECUTABLE
    "<CMAKE_C_COMPILER> ${MORPHOS_CLIB_FLAGS} <FLAGS> <CMAKE_C_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>")
