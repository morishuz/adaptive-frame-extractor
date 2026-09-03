if(NOT DEFINED CLI OR NOT DEFINED FIXTURE_TOOL OR NOT DEFINED SOURCE_DIR OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "cli_e2e.cmake requires CLI, FIXTURE_TOOL, SOURCE_DIR, and TEST_ROOT")
endif()
if(NOT DEFINED CONFIG_FILE)
  set(CONFIG_FILE "${SOURCE_DIR}/configs/profiles/medium.yaml")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(INPUT_VIDEO "${TEST_ROOT}/vfr_timing.mov")
set(OUTPUT_BASE "${TEST_ROOT}/output")

execute_process(
  COMMAND "${FIXTURE_TOOL}" "${SOURCE_DIR}/fixtures/vfr_timing.mov.b64" "${INPUT_VIDEO}"
  RESULT_VARIABLE fixture_result
  OUTPUT_VARIABLE fixture_stdout
  ERROR_VARIABLE fixture_stderr
)
if(NOT fixture_result EQUAL 0)
  message(FATAL_ERROR "fixture materialization failed: ${fixture_stdout}${fixture_stderr}")
endif()

execute_process(
  COMMAND "${CLI}" "${INPUT_VIDEO}"
    --config "${CONFIG_FILE}"
    --start-frame 1
    --max-frames 3
    --output-dir "${OUTPUT_BASE}"
  RESULT_VARIABLE cli_result
  OUTPUT_VARIABLE cli_stdout
  ERROR_VARIABLE cli_stderr
  TIMEOUT 30
)
if(NOT cli_result EQUAL 0)
  message(FATAL_ERROR "CLI failed: ${cli_stdout}${cli_stderr}")
endif()

file(GLOB run_dirs LIST_DIRECTORIES true "${OUTPUT_BASE}/*")
list(LENGTH run_dirs run_count)
if(NOT run_count EQUAL 1)
  message(FATAL_ERROR "expected one output run, found ${run_count}")
endif()
list(GET run_dirs 0 run_dir)

foreach(required_file config.yaml keyframes.csv summary.txt)
  if(NOT EXISTS "${run_dir}/${required_file}")
    message(FATAL_ERROR "missing output file: ${required_file}")
  endif()
endforeach()
foreach(required_image keyframe_0000_000001.jpg keyframe_0001_000003.jpg)
  if(NOT EXISTS "${run_dir}/keyframes/${required_image}")
    message(FATAL_ERROR "missing keyframe image: ${required_image}")
  endif()
endforeach()

file(READ "${run_dir}/keyframes.csv" manifest)
foreach(expected
    "keyframes/keyframe_0000_000001.jpg,0,1,0,24,0.04,ok,first,0,1")
  string(FIND "${manifest}" "${expected}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "manifest is missing expected row: ${expected}\n${manifest}")
  endif()
endforeach()
string(REGEX MATCH
  "keyframes/keyframe_0001_000003\\.jpg,1,3,0,96,0\\.16,ok,final,[0-9.eE+-]+,1"
  final_row "${manifest}")
if(final_row STREQUAL "")
  message(FATAL_ERROR "manifest is missing the expected final row\n${manifest}")
endif()

file(READ "${run_dir}/summary.txt" summary)
foreach(expected_pattern
    "Frame Extractor - Run Summary"
    "Frames processed: +3"
    "Keyframes saved: +2"
    "Starting frame: +1"
    "Maximum frames: +3"
    "Manifest: +keyframes\\.csv \\(schema 5\\)"
    "Backend: +libav"
    "PTS time base: +1/600"
    "Status: +OK")
  string(REGEX MATCH "${expected_pattern}" match "${summary}")
  if(match STREQUAL "")
    message(FATAL_ERROR
      "summary does not match: ${expected_pattern}\n${summary}")
  endif()
endforeach()

file(REMOVE_RECURSE "${TEST_ROOT}")
