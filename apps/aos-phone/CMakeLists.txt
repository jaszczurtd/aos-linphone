# aos-phone: SIP phone for AROS with a Zune interface, linked against the SDK
# installed by scripts/build-sdk.sh. Configure it with the same prefix and
# initial cache as the SDK:
#
#   cmake -S apps/aos-phone -B build-phone -G Ninja \
#         -DAOS_PREFIX=<prefix> -C sdk/aros-sdk.cmake \
#         -DCMAKE_TOOLCHAIN_FILE=<repo>/cmake/aros-x86_64.cmake \
#         -DAROS_TOOLCHAIN_DIR=<toolchain>
#
# AOS_TEST_RUNNER registers test_queue with CTest, as for the tests of the
# compatibility layer (see tests/CMakeLists.txt in the repository root).

cmake_minimum_required(VERSION 3.22)

project(aos-phone LANGUAGES C CXX)

# Each SDK package config loads its targets before it looks for its own
# dependencies: find the packages in dependency order (see aos-register).
foreach(pkg Iconv ZLIB LibXml2)
    find_package(${pkg} REQUIRED)
endforeach()
find_package(jsoncpp REQUIRED CONFIG)
foreach(pkg BCToolbox Belr BelleSIP Ortp Mediastreamer2 LibLinphone)
    find_package(${pkg} REQUIRED CONFIG)
endforeach()

set(AOS_PHONE_WARNINGS -Wall -Wextra -Werror)

# The device layer: the AHI sound card.
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../devices ${CMAKE_CURRENT_BINARY_DIR}/devices)

# The core thread: the only code that calls liblinphone.
add_library(aos_phone_core STATIC src/core.c src/queue.c)
target_compile_options(aos_phone_core PRIVATE ${AOS_PHONE_WARNINGS})
target_link_libraries(aos_phone_core PRIVATE liblinphone aos_devices)

add_executable(aos-phone src/main.c src/gui.c src/text.c)
target_compile_options(aos-phone PRIVATE ${AOS_PHONE_WARNINGS})
target_link_libraries(aos-phone PRIVATE aos_phone_core mui icon)
# liblinphone is C++: link with g++ so that libstdc++ comes along.
set_target_properties(aos-phone PROPERTIES LINKER_LANGUAGE CXX)

# The Workbench icon, made by the AROS host tool ilbmtoicon (built with AROS,
# in bin/<host>/tools of the AROS build directory). Without it the program
# builds without an icon.
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
    add_custom_target(aos-phone-icon ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/aos-phone.info)
else()
    message(STATUS "ilbmtoicon not found (AOS_ILBMTOICON): aos-phone.info is not built")
endif()

# Sends a command to the ARexx port of aos-phone without an ARexx interpreter.
add_executable(aos-phone-cmd src/cmd.c)
target_compile_options(aos-phone-cmd PRIVATE ${AOS_PHONE_WARNINGS})
target_link_libraries(aos-phone-cmd PRIVATE rexxsyslib)

include(CTest)
if(BUILD_TESTING)
    set(AOS_TEST_RUNNER "" CACHE STRING
        "Command (CMake list) that runs an AROS program and returns its exit code")
    add_executable(test_queue tests/test_queue.c src/queue.c)
    target_include_directories(test_queue PRIVATE src ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)
    target_compile_options(test_queue PRIVATE ${AOS_PHONE_WARNINGS})
    target_link_libraries(test_queue PRIVATE pthread)
    if(AOS_TEST_RUNNER)
        add_test(NAME test_queue COMMAND ${AOS_TEST_RUNNER} $<TARGET_FILE:test_queue>)
    endif()
endif()
