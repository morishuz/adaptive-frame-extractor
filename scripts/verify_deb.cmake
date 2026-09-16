# Run after apt installs the package on the CI runner.
cmake_minimum_required(VERSION 3.25)
foreach(required FIXTURE_TOOL TEST_ROOT)
  if(NOT DEFINED ${required} OR NOT IS_ABSOLUTE "${${required}}")
    message(FATAL_ERROR "Pass an absolute -D${required}=...")
  endif()
endforeach()
get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(ENV{LD_LIBRARY_PATH} "")
set(ENV{LD_PRELOAD} "")
set(CLI "/usr/bin/frame-extractor")
set(GUI "/usr/bin/frame-extractor-gui")
set(CONFIG_FILE "/opt/frame-extractor/bin/configs/medium.yaml")
foreach(resource
    /usr/bin/frame-extractor /usr/bin/frame-extractor-gui
    /usr/share/applications/io.github.morishuz.FrameExtractor.desktop
    /usr/share/icons/hicolor/256x256/apps/io.github.morishuz.FrameExtractor.png
    /opt/frame-extractor/bin/fonts/InterVariable.ttf
    /opt/frame-extractor/bin/icons/FrameExtractor.png
    /opt/frame-extractor/share/frame-extractor/licenses/third-party/sdl3.txt)
  if(NOT EXISTS "${resource}")
    message(FATAL_ERROR "Missing installed resource: ${resource}")
  endif()
endforeach()
find_program(_desktop_validate desktop-file-validate REQUIRED)
execute_process(COMMAND "${_desktop_validate}"
  /usr/share/applications/io.github.morishuz.FrameExtractor.desktop
  COMMAND_ERROR_IS_FATAL ANY)
# CMake resolves $ORIGIN relative to the supplied path, unlike the loader,
# which follows the /usr/bin symlink to the executable under /opt first.
# Keep runtime tests below using the public launchers, but scan real binaries.
file(REAL_PATH "${CLI}" _cli_binary)
file(REAL_PATH "${GUI}" _gui_binary)
file(GET_RUNTIME_DEPENDENCIES EXECUTABLES "${_cli_binary}" "${_gui_binary}"
  RESOLVED_DEPENDENCIES_VAR _resolved UNRESOLVED_DEPENDENCIES_VAR _unresolved)
if(_unresolved)
  message(FATAL_ERROR "Unresolved installed libraries: ${_unresolved}")
endif()
foreach(_library IN LISTS _resolved)
  file(REAL_PATH "${_library}" _library)
  if(NOT _library MATCHES "^/opt/frame-extractor/lib/|^/(usr/)?lib(64)?/")
    message(FATAL_ERROR "Installed app uses a build-tree library: ${_library}")
  endif()
endforeach()
foreach(executable "${CLI}" "${GUI}")
  execute_process(COMMAND "${executable}" --version
    TIMEOUT 30 COMMAND_ERROR_IS_FATAL ANY)
endforeach()
set(_test_root "${TEST_ROOT}")
set(TEST_ROOT "${_test_root}/cli")
include("${SOURCE_DIR}/tests/cli_e2e.cmake")
set(TEST_ROOT "${_test_root}/gui")
include("${SOURCE_DIR}/tests/gui_smoke.cmake")
message(STATUS "Installed Debian package passed extraction and GUI startup")
