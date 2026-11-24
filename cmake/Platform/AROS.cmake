# Platform description for AROS: Unix-like paths, static archives only.
set(UNIX 1)

set(CMAKE_STATIC_LIBRARY_PREFIX "lib")
set(CMAKE_STATIC_LIBRARY_SUFFIX ".a")
set(CMAKE_EXECUTABLE_SUFFIX "")
set(CMAKE_FIND_LIBRARY_PREFIXES "lib")
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")

set_property(GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS FALSE)

# Search the SDK root itself (include/, lib/); CMAKE_FIND_ROOT_PATH from the
# toolchain file re-roots "/" there.
list(APPEND CMAKE_SYSTEM_PREFIX_PATH "/")
