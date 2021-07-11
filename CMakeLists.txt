cmake_minimum_required(VERSION 3.20.5)

set(CMAKE_C_COMPILER /usr/bin/clang)
set(CMAKE_CXX_COMPILER /usr/bin/clang++)
set(CMAKE_EXPORT_COMPILE_COMMANDS true)

project(grayscale VERSION 1.0.0)

add_executable(grayscale main.c)

target_link_libraries (grayscale jpeg pthread)
