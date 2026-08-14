cmake_minimum_required(VERSION 3.20)
project(aos_p0_function_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(aos_p0_runner runner.cpp)
add_executable(aos_p0_fixture fixture.cpp)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
enable_testing()
add_test(NAME differential COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/differential.py
  --runner $<TARGET_FILE:aos_p0_runner> --fixture $<TARGET_FILE:aos_p0_fixture>
  --python-root ${CMAKE_CURRENT_SOURCE_DIR}/../p0-function-python)
