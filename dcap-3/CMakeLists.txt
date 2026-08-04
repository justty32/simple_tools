cmake_minimum_required(VERSION 3.20)
project(dcap CXX)

# Needs a compiler with C++20 and #embed (GCC 15+). Nothing else: no shell
# scripts, no code generation — src/builtin.cpp lists the embedded template
# files by hand.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Land the binary in bin/ so that PATH entry stays valid across rebuilds.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_executable(dcap src/main.cpp src/builtin.cpp)
target_compile_features(dcap PRIVATE cxx_std_20)
target_compile_options(dcap PRIVATE
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra>)

# Nothing lists templates/ as a build input: GCC records every #embed-ed file in
# its depfile, so editing a template rebuilds builtin.cpp on its own.
