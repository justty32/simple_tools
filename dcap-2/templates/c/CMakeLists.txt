cmake_minimum_required(VERSION 3.20)
project(@NAME@ C)

# ONE artifact that is both an executable and a shared library. It is linked as
# a .so (-shared -fPIC) but carries an embedded PT_INTERP and a custom entry
# point, so the kernel can run it directly AND other programs can link against
# it or dlopen it. @NAME@ was filled in by dcap.

# Path of the dynamic loader, embedded into the .interp section so the artifact
# is runnable. Override if detection fails:
#   cmake -B build -DDCAP_INTERP=/lib64/ld-linux-x86-64.so.2
if(NOT DCAP_INTERP)
  execute_process(COMMAND readelf -p .interp /bin/sh
                  OUTPUT_VARIABLE _readelf ERROR_QUIET)
  string(REGEX MATCH "/[^ \n]*ld-[^ \n]*" DCAP_INTERP "${_readelf}")
endif()
if(NOT DCAP_INTERP)
  message(FATAL_ERROR "cannot detect the dynamic loader; pass -DDCAP_INTERP=...")
endif()

# Every src/*.c goes into the one artifact; there is no naming rule.
add_library(main SHARED src/main.c src/lib.c)
target_include_directories(main PUBLIC include)
target_compile_features(main PUBLIC c_std_11)
target_compile_options(main PRIVATE -Wall -Wextra)
target_compile_definitions(main PRIVATE DCAP_INTERP="${DCAP_INTERP}")
target_link_libraries(main PRIVATE m pthread)

# -e makes it runnable; -soname makes it linkable as -l@NAME@. NO_SONAME stops
# CMake emitting a soname of its own; PREFIX/SUFFIX make the file plain `main`.
target_link_options(main PRIVATE -Wl,-e,dcap_main -Wl,-soname,lib@NAME@.so)
set_target_properties(main PROPERTIES PREFIX "" SUFFIX "" NO_SONAME TRUE)

# So that -L<builddir> -l@NAME@ resolves to the very same file.
add_custom_command(TARGET main POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink main lib@NAME@.so)
