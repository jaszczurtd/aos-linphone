# The device layer of aos-linphone: devices for mediastreamer2 on AROS. It
# has the AHI sound card, the video display that hands pictures to the user
# interface, the always-listed Mire test camera, a camera that takes its
# pictures from a source, sources that play raw files, and sources for the
# cameras of uvc.class (VHI). It does not use the compatibility layer. A program adds this directory
# after it has found the Mediastreamer2 package of the installed SDK, which
# also defines the libyuv target:
#
#   add_subdirectory(<repo>/devices ${CMAKE_CURRENT_BINARY_DIR}/devices)

add_library(aos_devices STATIC
    ahi/ahi_sound.c
    ahi/ahi_source.c
    files/file_source.c
    video/camera.c
    video/video_display.c
    video/mire_camera.c)
# uvc.class with its VHI libraries: part of AROS; on MorphOS the port in
# drivers/uvc, whose headers the camera source compiles against.
if(CMAKE_SYSTEM_NAME STREQUAL "AROS")
    target_sources(aos_devices PRIVATE vhi/vhi_camera.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "MorphOS")
    target_sources(aos_devices PRIVATE vhi/vhi_camera.c)
    target_include_directories(aos_devices PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../drivers/uvc/include)
else()
    target_sources(aos_devices PRIVATE vhi/vhi_none.c)
endif()
target_include_directories(aos_devices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(aos_devices PRIVATE -Wall -Wextra -Werror)
target_link_libraries(aos_devices PRIVATE mediastreamer2 yuv)
