# The device layer of aos-linphone: sound devices for mediastreamer2 on
# AROS (later also the camera and the video display). It does not use the
# compatibility layer. A program adds this directory after it has found the
# Mediastreamer2 package of the installed SDK:
#
#   add_subdirectory(<repo>/devices ${CMAKE_CURRENT_BINARY_DIR}/devices)

add_library(aos_devices STATIC ahi/ahi_sound.c)
target_include_directories(aos_devices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(aos_devices PRIVATE -Wall -Wextra -Werror)
target_link_libraries(aos_devices PRIVATE mediastreamer2)
