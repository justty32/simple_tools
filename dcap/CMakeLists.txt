cmake_minimum_required(VERSION 3.20)
project(dcap CXX)

# Needs a compiler with C++23 (#embed and std::expected — GCC 15+/Clang 19+
# with a matching stdlib). Nothing else: no shell scripts, no code generation
# — src/builtin.cpp lists the embedded template files by hand.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Land the binary in bin/ so that PATH entry stays valid across rebuilds.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

add_executable(dcap
  src/main.cpp
  src/builtin.cpp
  src/text_file.cpp
  src/template_source.cpp
  src/scaffold.cpp)
target_compile_features(dcap PRIVATE cxx_std_23)
target_compile_options(dcap PRIVATE
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra>)

# Nothing lists templates/ as a build input: GCC records every #embed-ed file in
# its depfile, so editing a template rebuilds builtin.cpp on its own.
