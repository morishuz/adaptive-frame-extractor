if(NOT DEFINED OUTPUT_FILE OR NOT DEFINED APP_VERSION OR NOT DEFINED SOURCE_DIR)
  message(FATAL_ERROR "OUTPUT_FILE, APP_VERSION, and SOURCE_DIR are required")
endif()

set(build_id "source")
if(DEFINED GIT_EXECUTABLE AND NOT GIT_EXECUTABLE STREQUAL "")
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=8 HEAD
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE revision_result
    OUTPUT_VARIABLE revision
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
  if(revision_result EQUAL 0 AND NOT revision STREQUAL "")
    set(build_id "${revision}")
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
      WORKING_DIRECTORY "${SOURCE_DIR}"
      RESULT_VARIABLE status_result
      OUTPUT_VARIABLE working_tree_status
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
    )
    if(status_result EQUAL 0 AND NOT working_tree_status STREQUAL "")
      string(APPEND build_id "-dirty")
    endif()
  endif()
endif()

set(contents "#pragma once\n\n#include <string_view>\n\nnamespace frame_extractor::build {\n\ninline constexpr std::string_view version{\"${APP_VERSION}\"};\ninline constexpr std::string_view id{\"${build_id}\"};\ninline constexpr std::string_view display{\"v${APP_VERSION} (${build_id})\"};\n\n}  // namespace frame_extractor::build\n")

get_filename_component(output_directory "${OUTPUT_FILE}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

set(previous_contents "")
if(EXISTS "${OUTPUT_FILE}")
  file(READ "${OUTPUT_FILE}" previous_contents)
endif()
if(NOT previous_contents STREQUAL contents)
  file(WRITE "${OUTPUT_FILE}" "${contents}")
endif()
