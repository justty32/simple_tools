cmake_minimum_required(VERSION 3.21)
project(@NAME@ C)

# TWO artifacts, laid out the conventional way: the executable in bin/ and the
# shared library in lib/. Nothing here is ELF-specific, so this builds on Linux,
# macOS and Windows. @NAME@ was filled in by dcap.

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Everything under src/ is library code except src/main.c, which is the
# executable's entry point. There is no other naming rule and subdirectories are
# fine. CONFIGURE_DEPENDS re-checks the glob on every build, so a newly added
# file is picked up without re-configuring by hand.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS src/*.c)
list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/main.c)

# The library target is named after the project so that two projects can be
# composed without a target-name clash — see the bottom of this file.
add_library(@NAME@ SHARED ${SOURCES})
target_include_directories(@NAME@ PUBLIC include)
target_compile_features(@NAME@ PUBLIC c_std_11)
target_compile_options(@NAME@ PRIVATE
  $<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-Wall;-Wextra>)

# WINDOWS_EXPORT_ALL_SYMBOLS saves writing __declspec(dllexport) in the sources.
# The output directories are the usual split: the .so/.dylib and the Windows
# import library go to lib/, the .dll to bin/ where the loader looks for it.
set_target_properties(@NAME@ PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS TRUE
  LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lib
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Installing is optional and off to the side of the normal build:
#   cmake --install build --prefix ~/.local
# GNUInstallDirs supplies bin/ lib/ include/, which is the layout this project
# already uses. These two rules sit outside the top-level guard below on
# purpose: a project pulled in with add_subdirectory has to install its library
# and headers too, or the installed executable has nothing to load.
include(GNUInstallDirs)
install(TARGETS @NAME@)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Only when this project is the one being built: a project that pulls this one
# in with add_subdirectory wants the library, not this executable.
if(PROJECT_IS_TOP_LEVEL)
  add_executable(@NAME@_main src/main.c)
  target_link_libraries(@NAME@_main PRIVATE @NAME@)
  set_target_properties(@NAME@_main PROPERTIES
    OUTPUT_NAME @NAME@
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

  # Once installed, the executable is no longer next to its build-tree library,
  # so it needs a relative rpath to the installed one. $ORIGIN is how ELF spells
  # "my own directory" and @loader_path is the Mach-O spelling; the one that
  # does not apply is just a path that is not there. Windows needs neither — the
  # .dll installs into bin/, right beside the executable.
  set_target_properties(@NAME@_main PROPERTIES INSTALL_RPATH
    "$ORIGIN/../${CMAKE_INSTALL_LIBDIR};@loader_path/../${CMAKE_INSTALL_LIBDIR}")
  install(TARGETS @NAME@_main)

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
# They are just directories — nothing has to be installed first and there is no
# find_package. Point at the directory and link its target, which is named after
# that project (a relative path works too):
#
#   add_subdirectory(/path/to/AAA AAA-build)
#   target_link_libraries(@NAME@ PRIVATE AAA)
#
# AAA's include/ comes with it, and AAA is built as part of this build.
