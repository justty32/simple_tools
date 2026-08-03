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

# Everything under src/ goes into the one artifact; there is no naming rule and
# subdirectories are fine. CONFIGURE_DEPENDS re-checks the glob on every build,
# so a newly added file is picked up without re-configuring by hand.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c)

# The target is named after the project, not `main`, so that two projects can
# be composed without a target-name clash. The file it produces is `main`.
add_library(@NAME@ SHARED ${SOURCES})
target_include_directories(@NAME@ PUBLIC include)
target_compile_features(@NAME@ PUBLIC c_std_11)
target_compile_options(@NAME@ PRIVATE -Wall -Wextra)
target_compile_definitions(@NAME@ PRIVATE DCAP_INTERP="${DCAP_INTERP}")
target_link_libraries(@NAME@ PRIVATE m pthread)

# -e makes it runnable; -soname makes it linkable as -l@NAME@. NO_SONAME stops
# CMake emitting a soname of its own; PREFIX/SUFFIX make the file plain `main`.
target_link_options(@NAME@ PRIVATE -Wl,-e,dcap_main -Wl,-soname,lib@NAME@.so)
set_target_properties(@NAME@ PROPERTIES
  OUTPUT_NAME main PREFIX "" SUFFIX "" NO_SONAME TRUE
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# The artifact sits in the project root, so ./main runs it and another project
# can point straight at <this dir>/lib@NAME@.so. The symlink is what makes the
# soname resolvable, both at link time and at run time.
add_custom_command(TARGET @NAME@ POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E create_symlink main lib@NAME@.so
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# --- Using another dcap project that lives in some local folder -------------
# They are just directories; no git, no install, no find_package. Point at the
# artifact by path and add its include/ (a relative path works too):
#
#   target_include_directories(@NAME@ PRIVATE /path/to/AAA/include)
#   target_link_libraries(@NAME@ PRIVATE /path/to/AAA/libAAA.so)
#
# CMake puts AAA's directory in this project's RPATH, so ./main just runs.
# Build AAA first — nothing here knows how to build it for you.
