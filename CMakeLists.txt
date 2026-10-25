cmake_minimum_required(VERSION 3.20)
project(LogAnalyzer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Увімкнення суворих попереджень за вимогами
add_compile_options(-Wall -Wextra -Wpedantic)

# Вказуємо шлях до заголовкових файлів
include_directories(include)

# Ціль для Етапу 0
add_executable(stage0_seq src/stage0_seq.cpp)
