cmake_minimum_required(VERSION 3.21)

project(aos-cpp LANGUAGES C CXX)

if(WIN32)
    message(FATAL_ERROR "aos-cpp supports POSIX platforms only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# -fvisibility=hidden 只管一般函式；inline 與樣板實體化是弱符號，要
# -fvisibility-inlines-hidden 才蓋得住。少了它，nlohmann 的樣板實體會出現在
# libaos.so 的動態符號表上，這個 .so 就變成「只給用同一版 nlohmann 的人用」。
add_compile_options(-fvisibility=hidden -fvisibility-inlines-hidden)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(Catch2 CONFIG REQUIRED)

set(AOS_OBJECT_SOURCES
    src/inst.cpp
    src/format.cpp
    src/exec.cpp
    src/capi.cpp
)
if(AOS_OBJECT_SOURCES)
    add_library(aos_objects OBJECT ${AOS_OBJECT_SOURCES})
    set_target_properties(aos_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(aos_objects PUBLIC include)
    target_link_libraries(aos_objects PRIVATE nlohmann_json::nlohmann_json)
endif()

add_library(aos SHARED)
target_link_libraries(aos PRIVATE aos_objects)
target_include_directories(aos PUBLIC include)
set_target_properties(aos PROPERTIES VERSION 0.1.0 SOVERSION 0)

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

add_executable(test_capi tests/test_capi.c)
set_target_properties(test_capi PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
)
target_link_libraries(test_capi PRIVATE aos)
add_test(NAME test_capi COMMAND test_capi)
