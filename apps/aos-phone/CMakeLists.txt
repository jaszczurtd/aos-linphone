# aos-phone: SIP phone for AROS with a Zune interface, linked against the SDK
# installed by scripts/build-sdk.sh. Configure it with the same prefix and
# initial cache as the SDK:
#
#   cmake -S apps/aos-phone -B build-phone -G Ninja \
#         -DAOS_PREFIX=<prefix> -C sdk/aros-sdk.cmake \
#         -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/aros-x86_64.cmake \
#         -DAROS_TOOLCHAIN_DIR=<toolchain>
#
# AOS_TEST_RUNNER registers the tests with CTest, as for the tests of the
# compatibility layer (see tests/CMakeLists.txt in the repository root).

cmake_minimum_required(VERSION 3.22)

project(aos-phone LANGUAGES C CXX)

# Each SDK package config loads its targets before it looks for its own
# dependencies: find the packages in dependency order (see aos-register).
set(AOS_SYSTEM_PACKAGES Iconv ZLIB LibXml2)
if(CMAKE_SYSTEM_NAME STREQUAL "MorphOS")
    list(REMOVE_ITEM AOS_SYSTEM_PACKAGES ZLIB) # the MorphOS SDK build has no zlib
endif()
foreach(pkg IN LISTS AOS_SYSTEM_PACKAGES)
    find_package(${pkg} REQUIRED)
endforeach()
find_package(jsoncpp REQUIRED CONFIG)
foreach(pkg BCToolbox Belr BelleSIP Ortp Mediastreamer2 LibLinphone)
    find_package(${pkg} REQUIRED CONFIG)
endforeach()

set(AOS_PHONE_WARNINGS -Wall -Wextra -Werror)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/version.cmake)
set(AOS_VERSION_DEFINES AOS_VERSION="${AOS_VERSION}" AOS_VERSION_DATE="${AOS_VERSION_DATE}")

# The device layer: the AHI sound card, the video display, the cameras and
# the file sources.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../devices ${CMAKE_CURRENT_BINARY_DIR}/devices)

# The core thread: the only code that calls liblinphone.
add_library(aos_phone_core STATIC src/core.c src/queue.c)
target_compile_options(aos_phone_core PRIVATE ${AOS_PHONE_WARNINGS})
target_link_libraries(aos_phone_core PRIVATE liblinphone aos_devices)

add_executable(aos-phone src/main.c src/gui.c src/lamp.c src/text.c src/video.c)
target_compile_options(aos-phone PRIVATE ${AOS_PHONE_WARNINGS})
target_compile_definitions(aos-phone PRIVATE ${AOS_VERSION_DEFINES})
# The stub libraries of the system calls: AROS links them, while on
# MorphOS the calls are inline and libauto opens the libraries.
if(CMAKE_SYSTEM_NAME STREQUAL "AROS")
    set(AOS_GUI_SYSTEM_LIBS mui icon cybergraphics)
    set(AOS_REXX_SYSTEM_LIBS rexxsyslib)
endif()
target_link_libraries(aos-phone PRIVATE aos_phone_core aos_devices ${AOS_GUI_SYSTEM_LIBS})
# SDI headers (sdi/SDI/): hook and dispatcher macros for AROS and MorphOS.
target_include_directories(aos-phone PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdi)
# liblinphone is C++: link with g++ so that libstdc++ comes along.
set_target_properties(aos-phone PROPERTIES LINKER_LANGUAGE CXX)

# The Workbench icons, made by the AROS host tool ilbmtoicon (built with AROS,
# in bin/<host>/tools of the AROS build directory). Without it the program
# builds without icons.
find_program(AOS_ILBMTOICON ilbmtoicon DOC "AROS host tool ilbmtoicon, which builds aos-phone.info")
if(AOS_ILBMTOICON)
    set(icon_dir ${CMAKE_CURRENT_SOURCE_DIR}/icons)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/aos-phone.info
        COMMAND ${AOS_ILBMTOICON} ${icon_dir}/aos-phone.info.src ${icon_dir}/aos-phone.png
                ${CMAKE_CURRENT_BINARY_DIR}/aos-phone.info
        DEPENDS ${icon_dir}/aos-phone.info.src ${icon_dir}/aos-phone.png
        COMMENT "Creating aos-phone.info"
        VERBATIM)
    # The icon of the drawer the programs live in, with the same picture.
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/aos-linphone.info
        COMMAND ${AOS_ILBMTOICON} ${icon_dir}/aos-linphone.info.src ${icon_dir}/aos-phone.png
                ${CMAKE_CURRENT_BINARY_DIR}/aos-linphone.info
        DEPENDS ${icon_dir}/aos-linphone.info.src ${icon_dir}/aos-phone.png
        COMMENT "Creating aos-linphone.info"
        VERBATIM)
    add_custom_target(aos-phone-icon ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/aos-phone.info
                                                 ${CMAKE_CURRENT_BINARY_DIR}/aos-linphone.info)
else()
    message(STATUS "ilbmtoicon not found (AOS_ILBMTOICON): aos-phone.info is not built")
endif()

# Sends a command to the ARexx port of aos-phone without an ARexx interpreter.
add_executable(aos-phone-cmd src/cmd.c)
target_compile_options(aos-phone-cmd PRIVATE ${AOS_PHONE_WARNINGS})
target_compile_definitions(aos-phone-cmd PRIVATE ${AOS_VERSION_DEFINES})
target_link_libraries(aos-phone-cmd PRIVATE ${AOS_REXX_SYSTEM_LIBS})

include(CTest)
if(BUILD_TESTING)
    set(AOS_TEST_RUNNER "" CACHE STRING
        "Command (CMake list) that runs an AROS program and returns its exit code")
    add_executable(test_queue tests/test_queue.c src/queue.c)
    target_include_directories(test_queue PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)
    target_compile_options(test_queue PRIVATE ${AOS_PHONE_WARNINGS})
    target_link_libraries(test_queue PRIVATE pthread)
    # The video display of the device layer, with the filter from a
    # mediastreamer2 factory.
    add_executable(test_video_display ../../devices/tests/test_video_display.c)
    target_include_directories(test_video_display PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)
    target_compile_options(test_video_display PRIVATE ${AOS_PHONE_WARNINGS})
    target_link_libraries(test_video_display PRIVATE aos_devices mediastreamer2)
    set_target_properties(test_video_display PROPERTIES LINKER_LANGUAGE CXX)
    # The file sources, the camera and the capture part of the AHI card, run
    # on an MSTicker.
    add_executable(test_capture ../../devices/tests/test_capture.c)
    target_include_directories(test_capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)
    target_compile_options(test_capture PRIVATE ${AOS_PHONE_WARNINGS})
    target_link_libraries(test_capture PRIVATE aos_devices mediastreamer2 pthread)
    set_target_properties(test_capture PROPERTIES LINKER_LANGUAGE CXX)
    # Name resolution as the phone does it: getaddrinfo() and the DNS
    # resolver of belle-sip (A, SRV); needs a DNS server that reaches the
    # internet.
    add_executable(test_dns tests/test_dns.c)
    target_include_directories(test_dns PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)
    target_compile_options(test_dns PRIVATE ${AOS_PHONE_WARNINGS})
    target_link_libraries(test_dns PRIVATE liblinphone)
    set_target_properties(test_dns PROPERTIES LINKER_LANGUAGE CXX)
    # HTTPS through belle-sip's HTTP client (TCP, TLS, certificate check),
    # which liblinphone's account services use; needs the internet and
    # share/linphone/rootca.pem next to the program.
    add_executable(test_https tests/test_https.c)
    target_include_directories(test_https PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)
    target_compile_options(test_https PRIVATE ${AOS_PHONE_WARNINGS})
    target_link_libraries(test_https PRIVATE liblinphone)
    set_target_properties(test_https PROPERTIES LINKER_LANGUAGE CXX)
    # The microphone source on ahi.device and the uvc.class cameras; the
    # camera part is skipped when AROS has no uvc0.vhi. AROS only: it needs
    # the uvc.class headers.
    if(CMAKE_SYSTEM_NAME STREQUAL "AROS")
        add_executable(test_hw_sources ../../devices/tests/test_hw_sources.c)
        target_include_directories(test_hw_sources PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)
        target_compile_options(test_hw_sources PRIVATE ${AOS_PHONE_WARNINGS})
        target_link_libraries(test_hw_sources PRIVATE aos_devices mediastreamer2 pthread)
        set_target_properties(test_hw_sources PROPERTIES LINKER_LANGUAGE CXX)
    endif()
    if(AOS_TEST_RUNNER)
        add_test(NAME test_queue COMMAND ${AOS_TEST_RUNNER} $<TARGET_FILE:test_queue>)
        add_test(NAME test_video_display COMMAND ${AOS_TEST_RUNNER} $<TARGET_FILE:test_video_display>)
        add_test(NAME test_capture COMMAND ${AOS_TEST_RUNNER} $<TARGET_FILE:test_capture>)
        if(TARGET test_hw_sources)
            add_test(NAME test_hw_sources COMMAND ${AOS_TEST_RUNNER} $<TARGET_FILE:test_hw_sources>)
        endif()
        add_test(NAME test_dns COMMAND ${AOS_TEST_RUNNER} $<TARGET_FILE:test_dns>)
        add_test(NAME test_https COMMAND ${AOS_TEST_RUNNER} $<TARGET_FILE:test_https>)
    endif()
endif()
