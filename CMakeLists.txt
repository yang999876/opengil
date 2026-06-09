cmake_minimum_required(VERSION 3.20)

project(openGil VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(opengil_core
  src/core/gil.cpp
  src/core/sha256.cpp
  src/core/wire.cpp
  src/semantic/semantic.cpp
)

target_include_directories(opengil_core PUBLIC include)

if(MSVC)
  target_compile_options(opengil_core PRIVATE /W4 /permissive-)
else()
  target_compile_options(opengil_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(opengil src/cli/main.cpp)
target_link_libraries(opengil PRIVATE opengil_core)

enable_testing()

add_executable(opengil_core_tests tests/unit/test_core.cpp)
target_link_libraries(opengil_core_tests PRIVATE opengil_core)
add_test(NAME opengil_core_tests COMMAND opengil_core_tests)

