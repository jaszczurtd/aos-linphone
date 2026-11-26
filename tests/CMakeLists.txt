# Tests run on the target system (AROS or MorphOS). AOS_TEST_RUNNER is a
# command that uploads a program to a running system, runs it with the given
# arguments and returns its exit code, for example:
#
#   -DAOS_TEST_RUNNER="/path/to/runner;--timeout;120"
#
# Without it the tests are only built.

set(AOS_TEST_RUNNER "" CACHE STRING
    "Command (CMake list) that runs a program on the target and returns its exit code")

set(AOS_TESTS
    test_net_socket.c
    test_net_resolve.c
    test_net_wait.c
    test_compat.c
    test_cxx_names.cpp
    test_cxx_runtime.cpp
)

foreach(source IN LISTS AOS_TESTS)
    get_filename_component(name "${source}" NAME_WE)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE aosnet aoscompat aos_warnings pthread)
    if(AOS_TEST_RUNNER)
        add_test(NAME ${name} COMMAND ${AOS_TEST_RUNNER} $<TARGET_FILE:${name}>)
    endif()
endforeach()

if(NOT AOS_TEST_RUNNER)
    message(STATUS "AOS_TEST_RUNNER not set: tests are built but not registered with CTest")
endif()
