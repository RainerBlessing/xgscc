cmake_minimum_required(VERSION 3.10)
project(xgscc VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# The SX52 core with the assembler and the disassembler
add_subdirectory(sxcpu)

add_library(xgscc_lib STATIC
    src/codegen.cpp
    src/compiler.cpp
    src/lexer.cpp
    src/ast.cpp
    src/parser.cpp
    src/runtime.cpp
    src/hexwriter.cpp
    src/memorymap.cpp
)
target_include_directories(xgscc_lib PUBLIC src)
target_link_libraries(xgscc_lib sxcpu)
target_compile_options(xgscc_lib PRIVATE -Wall -Wextra)

add_executable(xgscc src/main.cpp)
target_link_libraries(xgscc xgscc_lib)

option(BUILD_TESTS "Build the unit tests" ON)
enable_testing()
if(BUILD_TESTS)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    file(GLOB TEST_SOURCES "tests/*.cpp")
    add_executable(unit_tests ${TEST_SOURCES})
    target_include_directories(unit_tests PRIVATE tests)
    target_compile_definitions(unit_tests PRIVATE
        XGSCC_PATH="$<TARGET_FILE:xgscc>"
        EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples"
    )
    # the command line tests start the program
    add_dependencies(unit_tests xgscc)
    target_link_libraries(unit_tests gtest gtest_main xgscc_lib)
    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()
