if(NOT DEFINED CLI OR NOT DEFINED FIXTURE_TOOL OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR
    "selection_regression.cmake requires CLI, FIXTURE_TOOL, SOURCE_DIR, and TEST_ROOT")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(input_video "${TEST_ROOT}/long_cfr_h264.mp4")
set(output_base "${TEST_ROOT}/output")

execute_process(
  COMMAND "${FIXTURE_TOOL}"
    "${SOURCE_DIR}/fixtures/long_cfr_h264.mp4.b64" "${input_video}"
  RESULT_VARIABLE fixture_result
  OUTPUT_VARIABLE fixture_stdout
  ERROR_VARIABLE fixture_stderr
)
if(NOT fixture_result EQUAL 0)
  message(FATAL_ERROR
    "Fixture materialization failed: ${fixture_stdout}${fixture_stderr}")
endif()

execute_process(
  COMMAND "${CLI}" "${input_video}"
    --config "${SOURCE_DIR}/configs/profiles/medium.yaml"
    --output-dir "${output_base}"
  RESULT_VARIABLE cli_result
  OUTPUT_VARIABLE cli_stdout
  ERROR_VARIABLE cli_stderr
  TIMEOUT 30
)
if(NOT cli_result EQUAL 0)
  message(FATAL_ERROR "CLI failed: ${cli_stdout}${cli_stderr}")
endif()

file(GLOB run_directories LIST_DIRECTORIES true "${output_base}/*")
list(LENGTH run_directories run_count)
if(NOT run_count EQUAL 1)
  message(FATAL_ERROR "Expected one output run, found ${run_count}")
endif()
list(GET run_directories 0 run_directory)
file(STRINGS "${run_directory}/keyframes.csv" manifest_rows)
list(POP_FRONT manifest_rows manifest_header)

set(expected_rows
  "^keyframes/keyframe_0000_000000\\.jpg,0,0,0,[^,]*,[^,]*,ok,first,"
  "^keyframes/keyframe_0001_000153\\.jpg,1,153,0,[^,]*,[^,]*,ok,low_points,"
  "^keyframes/keyframe_0002_000299\\.jpg,2,299,0,[^,]*,[^,]*,ok,final,"
)
list(LENGTH manifest_rows actual_count)
list(LENGTH expected_rows expected_count)
if(NOT actual_count EQUAL expected_count)
  message(FATAL_ERROR
    "Expected ${expected_count} selected frames, found ${actual_count}: ${manifest_rows}")
endif()

math(EXPR last_index "${expected_count} - 1")
foreach(index RANGE 0 ${last_index})
  list(GET manifest_rows ${index} actual_row)
  list(GET expected_rows ${index} expected_pattern)
  if(NOT actual_row MATCHES "${expected_pattern}")
    message(FATAL_ERROR
      "Selection ${index} changed.\nExpected: ${expected_pattern}\nActual:   ${actual_row}")
  endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
