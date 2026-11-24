# Host tools used by the tests (built with the host compiler, not for AROS).
#
#   cmake -S tools -B build-tools && cmake --build build-tools

cmake_minimum_required(VERSION 3.16)

project(aos-linphone-tools LANGUAGES C)

if(CMAKE_SYSTEM_NAME STREQUAL "AROS")
    message(FATAL_ERROR "tools/ is built for the host; configure it without the AROS toolchain file")
endif()

find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_executable(sip-registrar sip-registrar/sip_registrar.c)
target_compile_options(sip-registrar PRIVATE -Wall -Wextra -Werror)
target_link_libraries(sip-registrar PRIVATE OpenSSL::Crypto)
