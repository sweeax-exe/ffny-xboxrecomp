cmake_minimum_required(VERSION 3.20)
project(nv2a_small_capture_regression C)
enable_testing()
add_executable(nv2a-small-capture-test test_main.c
    ../../src/kernel/nv2a_pb_exec.c
    ../../src/kernel/nv2a_vsh_interp.c
    ../../src/kernel/nv2a_combiner.c
    ../../src/kernel/nv2a_flip_hold.c
    ../nv2a_exec_stubs/fork_stubs.c)   # the Def Jam fork's translator calls, inert
target_include_directories(nv2a-small-capture-test PRIVATE ../../src/kernel ../../src)
if(MSVC)
    target_compile_definitions(nv2a-small-capture-test PRIVATE _CRT_SECURE_NO_WARNINGS)
elseif(NOT WIN32)
    # The fork's executor reads the host clock through the Win32 vocabulary.
    add_subdirectory(../../src/platform platform)
    target_link_libraries(nv2a-small-capture-test PRIVATE platform m pthread)
endif()
add_test(NAME nv2a-small-capture COMMAND nv2a-small-capture-test)
