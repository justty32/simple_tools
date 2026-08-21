cmake_minimum_required(VERSION 3.21)

project(aos-cpp LANGUAGES CXX)

if(WIN32)
    message(FATAL_ERROR "aos-cpp supports POSIX platforms only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-fvisibility=hidden)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

set(AOS_OBJECT_SOURCES
    src/inst.cpp
    src/format.cpp
    src/exec.cpp
)
if(AOS_OBJECT_SOURCES)
    add_library(aos_objects OBJECT ${AOS_OBJECT_SOURCES})
    target_include_directories(aos_objects PUBLIC include)
    target_link_libraries(aos_objects PRIVATE nlohmann_json::nlohmann_json)
endif()

add_executable(aos-cpp
    src/main.cpp
    src/run.cpp
)
target_link_libraries(aos-cpp PRIVATE aos_objects)

enable_testing()

add_executable(aos_tests
    tests/test_format_read.cpp
    tests/test_format_write.cpp
    tests/test_format_limits.cpp
    tests/test_format_malformed.cpp
    tests/test_exec.cpp
    tests/test_timeout.cpp
    tests/test_run.cpp
    src/run.cpp
)
target_link_libraries(aos_tests PRIVATE aos_objects Catch2::Catch2WithMain)
add_test(NAME aos_tests COMMAND aos_tests)
