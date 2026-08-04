cmake_minimum_required(VERSION 3.21)
project(@NAME@ CXX)

# TWO artifacts, laid out the conventional way: the executable in bin/ and the
# shared library in lib/. Nothing here is ELF-specific, so this builds on Linux,
# macOS and Windows. @NAME@ was filled in by dcap.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Everything under src/ is library code except src/main.cpp, which is the
# executable's entry point. There is no other naming rule and subdirectories are
# fine. CONFIGURE_DEPENDS re-checks the glob on every build, so a newly added
# file is picked up without re-configuring by hand.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.cpp)
list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)

# The library target is named after the project so that two projects can be
# composed without a target-name clash — see the bottom of this file.
add_library(@NAME@ SHARED ${SOURCES})
target_include_directories(@NAME@ PUBLIC include)
target_compile_features(@NAME@ PUBLIC cxx_std_20)
target_compile_options(@NAME@ PRIVATE
  $<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-Wall;-Wextra>)

# WINDOWS_EXPORT_ALL_SYMBOLS saves writing __declspec(dllexport) in the sources.
# The output directories are the usual split: the .so/.dylib and the Windows
# import library go to lib/, the .dll to bin/ where the loader looks for it.
set_target_properties(@NAME@ PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS TRUE
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Only when this project is the one being built: a project that pulls this one
# in with add_subdirectory wants the library, not this executable.
if(PROJECT_IS_TOP_LEVEL)
  add_executable(@NAME@_main src/main.cpp)
  target_link_libraries(@NAME@_main PRIVATE @NAME@)
  set_target_properties(@NAME@_main PROPERTIES
    OUTPUT_NAME @NAME@
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

  # Windows has no RPATH. This project's own .dll is already in bin/, but a
  # linked-in project's .dll sits in that project's bin/, where the loader will
  # not look. Copy those across. Elsewhere CMake writes the directories into the
  # build-tree RPATH for us.
  if(WIN32)
    add_custom_command(TARGET @NAME@_main POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:@NAME@_main>
              $<TARGET_RUNTIME_DLLS:@NAME@_main>
      COMMAND_EXPAND_LISTS)
  endif()
endif()

# --- Using another dcap project that lives in some local folder -------------
# They are just directories; no git, no install, no find_package. Point at the
# directory and link its target — that target is named after the project (a
# relative path works too):
#
#   add_subdirectory(/path/to/AAA AAA-build)
#   target_link_libraries(@NAME@ PRIVATE AAA)
#
# AAA's include/ comes with it, and AAA is built as part of this build.
