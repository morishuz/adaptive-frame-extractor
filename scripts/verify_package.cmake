# Verify the archive after extraction to a fresh path containing spaces. Do not
# run from the build directory: that can accidentally mask missing resources/DLLs.
cmake_minimum_required(VERSION 3.25)
foreach(required ARCHIVE FIXTURE_TOOL TEST_ROOT)
  if(NOT DEFINED ${required} OR NOT IS_ABSOLUTE "${${required}}")
    message(FATAL_ERROR "Pass an absolute -D${required}=... to verify_package.cmake")
  endif()
endforeach()
get_filename_component(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
string(RANDOM LENGTH 12 _suffix)
set(_work "${TEST_ROOT}/package check ${_suffix}")
file(MAKE_DIRECTORY "${_work}")
file(ARCHIVE_EXTRACT INPUT "${ARCHIVE}" DESTINATION "${_work}/unpacked")
file(GLOB _roots LIST_DIRECTORIES TRUE "${_work}/unpacked/*")
list(LENGTH _roots _count)
if(NOT _count EQUAL 1)
  message(FATAL_ERROR "Expected one top-level package directory")
endif()
list(GET _roots 0 _package)
set(_suffix "")
if(WIN32)
  set(_suffix .exe)
  # Do not inherit developer/vcpkg directories. Windows 10+ provides the OS DLLs.
  set(ENV{PATH} "$ENV{SystemRoot}/System32;$ENV{SystemRoot}")
  file(GLOB _libraries "${_package}/bin/*.dll")
else()
  set(ENV{LD_LIBRARY_PATH} "")
  set(ENV{LD_PRELOAD} "")
  file(GLOB _libraries "${_package}/lib/*.so*")
endif()
if(NOT _libraries)
  message(FATAL_ERROR "No runtime libraries were packaged")
endif()
foreach(resource bin/fonts/InterVariable.ttf bin/icons/FrameExtractor.png
    bin/configs/low.yaml bin/configs/medium.yaml bin/configs/high.yaml
    share/frame-extractor/LICENSE share/frame-extractor/THIRD_PARTY_NOTICES.md
    share/frame-extractor/licenses/Inter-LICENSE.txt
    share/frame-extractor/licenses/ImGui-LICENSE.txt
    share/frame-extractor/licenses/third-party/ffmpeg.txt
    share/frame-extractor/licenses/third-party/opencv4.txt
    share/frame-extractor/licenses/third-party/sdl3.txt
    share/frame-extractor/licenses/third-party/yaml-cpp.txt)
  if(NOT EXISTS "${_package}/${resource}")
    message(FATAL_ERROR "Missing packaged resource: ${resource}")
  endif()
endforeach()
set(CLI "${_package}/bin/frame-extractor${_suffix}")
set(GUI "${_package}/bin/frame-extractor-gui${_suffix}")
if(UNIX AND NOT APPLE)
  file(GET_RUNTIME_DEPENDENCIES EXECUTABLES "${CLI}" "${GUI}"
    RESOLVED_DEPENDENCIES_VAR _resolved UNRESOLVED_DEPENDENCIES_VAR _unresolved)
  if(_unresolved)
    message(FATAL_ERROR "Unresolved packaged libraries: ${_unresolved}")
  endif()
  foreach(_library IN LISTS _resolved)
    string(FIND "${_library}" "${_package}/" _in_package)
    if(NOT _in_package EQUAL 0 AND NOT _library MATCHES "^/(usr/)?lib(64)?/")
      message(FATAL_ERROR "Package still uses a non-system external library: ${_library}")
    endif()
  endforeach()
endif()
foreach(executable "${CLI}" "${GUI}")
  execute_process(COMMAND "${executable}" --version WORKING_DIRECTORY "${_work}"
    TIMEOUT 30 COMMAND_ERROR_IS_FATAL ANY)
endforeach()
set(CONFIG_FILE "${_package}/bin/configs/medium.yaml")
set(TEST_ROOT "${_work}/cli output")
include("${SOURCE_DIR}/tests/cli_e2e.cmake")
set(TEST_ROOT "${_work}/gui startup")
include("${SOURCE_DIR}/tests/gui_smoke.cmake")

# Ensure startup cannot silently fall back to a system/default font in packages.
set(_font "${_package}/bin/fonts/InterVariable.ttf")
file(RENAME "${_font}" "${_font}.saved")
execute_process(COMMAND "${GUI}" --smoke-test "${_work}/unused.mov"
  WORKING_DIRECTORY "${_work}" TIMEOUT 30 RESULT_VARIABLE _result ERROR_VARIABLE _stderr)
file(RENAME "${_font}.saved" "${_font}")
if(NOT _result STREQUAL "1" OR NOT _stderr MATCHES "Bundled Inter font could not be loaded")
  message(FATAL_ERROR "Missing bundled font was not detected (${_result}): ${_stderr}")
endif()

# The GUI must fail clearly rather than silently substitute compiled profile values.
set(_profile "${_package}/bin/configs/medium.yaml")
file(RENAME "${_profile}" "${_profile}.saved")
execute_process(COMMAND "${GUI}" --smoke-test "${_work}/unused.mov"
  WORKING_DIRECTORY "${_work}" TIMEOUT 30 RESULT_VARIABLE _result ERROR_VARIABLE _stderr)
file(RENAME "${_profile}.saved" "${_profile}")
if(NOT _result STREQUAL "1" OR NOT _stderr MATCHES "Profile configuration could not be loaded")
  message(FATAL_ERROR "Missing bundled profile was not detected (${_result}): ${_stderr}")
endif()
message(STATUS "Relocated package passed CLI extraction and GUI startup: ${ARCHIVE}")
# Remove only the unique directory this script created, retaining failures for diagnosis.
file(REMOVE_RECURSE "${_work}")
