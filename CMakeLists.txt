cmake_minimum_required(VERSION 3.20)
project(ConcurrencyLabLogan CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# Етап 0
add_executable(stage0_seq src/stage0_seq.cpp)
target_include_directories(stage0_seq PRIVATE include)

# Етап 1
add_executable(stage1_threads src/stage1_threads.cpp)
target_include_directories(stage1_threads PRIVATE include)