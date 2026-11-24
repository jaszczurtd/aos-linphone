cmake_minimum_required(VERSION 3.16)

project(aos-linphone LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "AROS")
    message(FATAL_ERROR
        "aos-linphone targets AROS; configure with "
        "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/aros-x86_64.cmake")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AOS_WERROR "Treat warnings in aos-linphone code as errors" ON)

# Warning flags for the targets of this project only.
add_library(aos_warnings INTERFACE)
target_compile_options(aos_warnings INTERFACE -Wall -Wextra $<$<BOOL:${AOS_WERROR}>:-Werror>)

include(GNUInstallDirs)

add_subdirectory(compat)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
