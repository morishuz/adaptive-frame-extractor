if(NOT DEFINED GUI OR NOT DEFINED FIXTURE_TOOL OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "gui_smoke.cmake requires GUI, FIXTURE_TOOL, SOURCE_DIR and TEST_ROOT")
endif()

file(MAKE_DIRECTORY "${TEST_ROOT}")
set(_video "${TEST_ROOT}/sample video.mov")
execute_process(
  COMMAND "${FIXTURE_TOOL}" "${SOURCE_DIR}/fixtures/vfr_timing.mov.b64" "${_video}"
  COMMAND_ERROR_IS_FATAL ANY)

execute_process(COMMAND "${GUI}" --smoke-test "${_video}"
  WORKING_DIRECTORY "${TEST_ROOT}" TIMEOUT 30
  RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
if(NOT _result STREQUAL "0" OR NOT _stdout MATCHES "GUI smoke test passed")
  message(FATAL_ERROR "GUI startup failed (${_result}):\n${_stdout}${_stderr}")
endif()
message(STATUS "${_stdout}")

# An invalid input must exit with an error, not hang behind a modal dialog.
execute_process(COMMAND "${GUI}" --smoke-test "${TEST_ROOT}/does-not-exist.mov"
  WORKING_DIRECTORY "${TEST_ROOT}" TIMEOUT 30
  RESULT_VARIABLE _result OUTPUT_VARIABLE _stdout ERROR_VARIABLE _stderr)
if(NOT _result STREQUAL "1" OR NOT _stderr MATCHES "GUI smoke test failed")
  message(FATAL_ERROR "GUI did not report invalid input correctly (${_result}):\n${_stdout}${_stderr}")
endif()
file(REMOVE "${_video}")
