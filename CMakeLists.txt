cmake_minimum_required(VERSION 3.25)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
  set(
    CMAKE_OSX_DEPLOYMENT_TARGET "14.0"
    CACHE STRING "Minimum supported macOS version" FORCE
  )
endif()

project(
  frame_extractor_desktop
  VERSION 0.2.0
  DESCRIPTION "Native frame extractor processing core and desktop application"
  LANGUAGES CXX
)

option(FRAME_EXTRACTOR_BUILD_TESTS "Build unit tests" ON)
option(FRAME_EXTRACTOR_BUILD_GUI "Build the SDL3/ImGui desktop application" ON)
option(FRAME_EXTRACTOR_GUI_SMOKE_TEST "Enable GUI tests requiring a display (or Xvfb)" OFF)
option(
  FRAME_EXTRACTOR_PACKAGE_STANDALONE
  "Bundle runtime dependencies for distribution (and sign on macOS)"
  OFF
)
set(
  FRAME_EXTRACTOR_MACOS_CODESIGN_IDENTITY "-"
  CACHE STRING "macOS signing identity; '-' performs ad-hoc signing"
)

set(FRAME_EXTRACTOR_MACOS_BUNDLE_NAME "Frame Extractor")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(CompilerWarnings)

if(FRAME_EXTRACTOR_PACKAGE_STANDALONE)
  if(APPLE AND NOT FRAME_EXTRACTOR_BUILD_GUI)
    message(FATAL_ERROR "The standalone macOS package requires the GUI application bundle")
  endif()
  if(NOT VCPKG_TARGET_TRIPLET)
    message(FATAL_ERROR "Standalone packages require the controlled vcpkg build; see README.md")
  endif()
  if(VCPKG_INSTALLED_DIR)
    set(_frame_extractor_vcpkg_root "${VCPKG_INSTALLED_DIR}")
  else()
    set(_frame_extractor_vcpkg_root "${CMAKE_BINARY_DIR}/vcpkg_installed")
  endif()
  set(FRAME_EXTRACTOR_VCPKG_PREFIX "${_frame_extractor_vcpkg_root}/${VCPKG_TARGET_TRIPLET}")
  if(UNIX AND NOT APPLE)
    set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")
    find_program(FRAME_EXTRACTOR_PATCHELF patchelf REQUIRED)
  endif()
endif()

find_package(yaml-cpp CONFIG REQUIRED)
find_package(OpenCV 4 REQUIRED COMPONENTS core imgcodecs imgproc video)
find_package(FFmpeg REQUIRED)
find_package(Threads REQUIRED)
find_package(Git QUIET)

set(
  FRAME_EXTRACTOR_BUILD_INFO_HEADER
  "${CMAKE_CURRENT_BINARY_DIR}/generated/frame_extractor/build_info.hpp"
)
set(_frame_extractor_build_info_command
  "${CMAKE_COMMAND}"
  "-DOUTPUT_FILE=${FRAME_EXTRACTOR_BUILD_INFO_HEADER}"
  "-DAPP_VERSION=${PROJECT_VERSION}"
  "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
  "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
  -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake"
)
execute_process(
  COMMAND ${_frame_extractor_build_info_command}
  COMMAND_ERROR_IS_FATAL ANY
)
add_custom_target(frame_extractor_build_info ALL
  COMMAND ${_frame_extractor_build_info_command}
  BYPRODUCTS "${FRAME_EXTRACTOR_BUILD_INFO_HEADER}"
  COMMENT "Refreshing Frame Extractor build information"
  VERBATIM
)

add_library(frame_extractor_core
  src/core/async_image_writer.cpp
  src/core/atomic_output_file.cpp
  src/core/config.cpp
  src/core/decoder.cpp
  src/core/extraction_run.cpp
  src/core/image_processing.cpp
  src/core/image_writer.cpp
  src/core/output.cpp
  src/core/processor.cpp
  src/core/run_files.cpp
  src/core/tracking.cpp
)
add_library(frame_extractor::core ALIAS frame_extractor_core)
add_dependencies(frame_extractor_core frame_extractor_build_info)

target_compile_features(frame_extractor_core PUBLIC cxx_std_20)
target_include_directories(frame_extractor_core
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(frame_extractor_core
  PUBLIC
    yaml-cpp::yaml-cpp
    ${OpenCV_LIBS}
    FFmpeg::FFmpeg
    Threads::Threads
)
target_include_directories(frame_extractor_core SYSTEM PUBLIC ${OpenCV_INCLUDE_DIRS})
frame_extractor_set_warnings(frame_extractor_core)

add_executable(frame_extractor_cli
  src/cli/main.cpp
)
set_target_properties(frame_extractor_cli PROPERTIES OUTPUT_NAME frame-extractor)
target_link_libraries(frame_extractor_cli PRIVATE frame_extractor::core)
add_dependencies(frame_extractor_cli frame_extractor_build_info)
frame_extractor_set_warnings(frame_extractor_cli)

if(FRAME_EXTRACTOR_BUILD_GUI)
  find_package(SDL3 CONFIG REQUIRED)
  include(FetchContent)
  FetchContent_Declare(
    frame_extractor_imgui_source
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG 8936b58fe26e8c3da834b8f60b06511d537b4c63
  )
  FetchContent_MakeAvailable(frame_extractor_imgui_source)

  add_library(frame_extractor_imgui STATIC
    ${frame_extractor_imgui_source_SOURCE_DIR}/imgui.cpp
    ${frame_extractor_imgui_source_SOURCE_DIR}/imgui_draw.cpp
    ${frame_extractor_imgui_source_SOURCE_DIR}/imgui_tables.cpp
    ${frame_extractor_imgui_source_SOURCE_DIR}/imgui_widgets.cpp
    ${frame_extractor_imgui_source_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${frame_extractor_imgui_source_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
  )
  target_include_directories(frame_extractor_imgui SYSTEM PUBLIC
    ${frame_extractor_imgui_source_SOURCE_DIR}
    ${frame_extractor_imgui_source_SOURCE_DIR}/backends
  )
  target_link_libraries(frame_extractor_imgui PUBLIC SDL3::SDL3)

  add_library(frame_extractor_gui_support STATIC
    src/gui/app_panels.cpp
    src/gui/app_preferences.cpp
    src/gui/extraction_controller.cpp
    src/gui/image_utils.cpp
    src/gui/keyframe_strip.cpp
    src/gui/manual_frame_exporter.cpp
    src/gui/platform.cpp
    src/gui/preview_state.cpp
    src/gui/profile_configs.cpp
    src/gui/project_state.cpp
    src/gui/smoke_test.cpp
    src/gui/texture.cpp
    src/gui/video_scrubber.cpp
    src/gui/widgets.cpp
  )
  target_include_directories(frame_extractor_gui_support PUBLIC src/gui)
  target_link_libraries(frame_extractor_gui_support
    PUBLIC
      frame_extractor::core
      frame_extractor_imgui
      Threads::Threads
  )
  frame_extractor_set_warnings(frame_extractor_gui_support)

  add_executable(frame_extractor_gui src/gui/main.cpp)
  if(APPLE)
    add_executable(frame_extractor_icns_builder EXCLUDE_FROM_ALL
      tools/macos_icns_builder.cpp
    )
    target_compile_features(frame_extractor_icns_builder PRIVATE cxx_std_20)
    frame_extractor_set_warnings(frame_extractor_icns_builder)

    set(_frame_extractor_icon "${CMAKE_CURRENT_BINARY_DIR}/icons/FrameExtractor.icns")
    add_custom_command(
      OUTPUT "${_frame_extractor_icon}"
      COMMAND /bin/sh "${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_macos_icon.sh"
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/icon.png"
        "${_frame_extractor_icon}"
        "$<TARGET_FILE:frame_extractor_icns_builder>"
      DEPENDS
        assets/icons/icon.png
        scripts/generate_macos_icon.sh
        frame_extractor_icns_builder
      COMMENT "Generating macOS application icon"
      VERBATIM
    )
    set_source_files_properties("${_frame_extractor_icon}" PROPERTIES
      MACOSX_PACKAGE_LOCATION "Resources"
    )
    set_target_properties(frame_extractor_gui PROPERTIES
      OUTPUT_NAME "${FRAME_EXTRACTOR_MACOS_BUNDLE_NAME}"
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "${FRAME_EXTRACTOR_MACOS_BUNDLE_NAME}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "com.drmo.frame-extractor"
      MACOSX_BUNDLE_ICON_FILE "FrameExtractor.icns"
      MACOSX_BUNDLE_INFO_STRING "Native motion-based keyframe extraction"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 M. Seeger"
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/macos/Info.plist.in"
    )
    target_sources(frame_extractor_gui PRIVATE
      "${_frame_extractor_icon}"
      assets/fonts/InterVariable.ttf
      assets/fonts/Inter-LICENSE.txt
      configs/profiles/low.yaml
      configs/profiles/medium.yaml
      configs/profiles/high.yaml
      LICENSE
      THIRD_PARTY_NOTICES.md
    )
    set_source_files_properties(assets/fonts/InterVariable.ttf PROPERTIES
      MACOSX_PACKAGE_LOCATION "Resources/fonts"
    )
    set_source_files_properties(assets/fonts/Inter-LICENSE.txt LICENSE THIRD_PARTY_NOTICES.md
      PROPERTIES MACOSX_PACKAGE_LOCATION "Resources/licenses"
    )
    set_source_files_properties(
      configs/profiles/low.yaml
      configs/profiles/medium.yaml
      configs/profiles/high.yaml
      PROPERTIES
      MACOSX_PACKAGE_LOCATION "Resources/configs"
    )
  else()
    set_target_properties(frame_extractor_gui PROPERTIES OUTPUT_NAME frame-extractor-gui)
    if(WIN32)
      set_target_properties(frame_extractor_gui PROPERTIES WIN32_EXECUTABLE TRUE)
      enable_language(RC)
      configure_file(cmake/windows/App.rc.in "${CMAKE_CURRENT_BINARY_DIR}/App.rc" @ONLY)
      set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/App.rc" PROPERTIES
        OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/FrameExtractor.ico")
      target_sources(frame_extractor_gui PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/App.rc")
    endif()
  endif()
  target_link_libraries(frame_extractor_gui
    PRIVATE
      frame_extractor_gui_support
  )
  add_dependencies(frame_extractor_gui frame_extractor_build_info)
  frame_extractor_set_warnings(frame_extractor_gui)
  if(NOT APPLE)
    set_property(TARGET frame_extractor_gui APPEND PROPERTY LINK_DEPENDS
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/InterVariable.ttf"
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/FrameExtractor.png"
      "${CMAKE_CURRENT_SOURCE_DIR}/configs/profiles/low.yaml"
      "${CMAKE_CURRENT_SOURCE_DIR}/configs/profiles/medium.yaml"
      "${CMAKE_CURRENT_SOURCE_DIR}/configs/profiles/high.yaml")
    add_custom_command(TARGET frame_extractor_gui POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:frame_extractor_gui>/fonts"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/InterVariable.ttf"
        "$<TARGET_FILE_DIR:frame_extractor_gui>/fonts/InterVariable.ttf"
      COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:frame_extractor_gui>/icons"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/FrameExtractor.png"
        "$<TARGET_FILE_DIR:frame_extractor_gui>/icons/FrameExtractor.png"
      COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:frame_extractor_gui>/configs"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/configs/profiles/low.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/configs/profiles/medium.yaml"
        "${CMAKE_CURRENT_SOURCE_DIR}/configs/profiles/high.yaml"
        "$<TARGET_FILE_DIR:frame_extractor_gui>/configs"
      VERBATIM
    )
  endif()
endif()

if(APPLE AND FRAME_EXTRACTOR_BUILD_GUI)
  install(TARGETS frame_extractor_gui BUNDLE DESTINATION .)
  install(
    TARGETS frame_extractor_cli
    RUNTIME DESTINATION "${FRAME_EXTRACTOR_MACOS_BUNDLE_NAME}.app/Contents/MacOS"
  )
  install(FILES "${frame_extractor_imgui_source_SOURCE_DIR}/LICENSE.txt"
    DESTINATION "${FRAME_EXTRACTOR_MACOS_BUNDLE_NAME}.app/Contents/Resources/licenses"
    RENAME ImGui-LICENSE.txt)
  if(FRAME_EXTRACTOR_PACKAGE_STANDALONE)
    set(
      FRAME_EXTRACTOR_MACOS_DEPENDENCY_DIR
      "${FRAME_EXTRACTOR_VCPKG_PREFIX}/lib"
    )
    configure_file(
      cmake/macos/FixupBundle.cmake.in
      "${CMAKE_CURRENT_BINARY_DIR}/FixupBundle.cmake"
      @ONLY
    )
    install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/FixupBundle.cmake")
  endif()
else()
  install(TARGETS frame_extractor_cli
    RUNTIME_DEPENDENCY_SET frame_extractor_runtime RUNTIME DESTINATION bin)
  if(FRAME_EXTRACTOR_BUILD_GUI)
    install(TARGETS frame_extractor_gui
      RUNTIME_DEPENDENCY_SET frame_extractor_runtime RUNTIME DESTINATION bin)
    install(FILES assets/fonts/InterVariable.ttf DESTINATION bin/fonts)
    install(FILES assets/icons/FrameExtractor.png DESTINATION bin/icons)
    install(DIRECTORY configs/profiles/ DESTINATION bin/configs)
    install(FILES assets/fonts/Inter-LICENSE.txt DESTINATION share/frame-extractor/licenses)
    install(FILES "${frame_extractor_imgui_source_SOURCE_DIR}/LICENSE.txt"
      DESTINATION share/frame-extractor/licenses RENAME ImGui-LICENSE.txt)
  endif()
  install(FILES LICENSE THIRD_PARTY_NOTICES.md DESTINATION share/frame-extractor)
  if(FRAME_EXTRACTOR_PACKAGE_STANDALONE)
    include(PortablePackage)
  endif()
endif()

set(CPACK_PACKAGE_NAME "frame-extractor")
set(CPACK_PACKAGE_VENDOR "M. Seeger")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native motion-based keyframe extraction")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
if(WIN32)
  set(CPACK_GENERATOR "ZIP")
elseif(APPLE AND FRAME_EXTRACTOR_BUILD_GUI)
  if(FRAME_EXTRACTOR_PACKAGE_STANDALONE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "Frame Extractor ${PROJECT_VERSION}")
    set(CPACK_PACKAGE_FILE_NAME "Frame-Extractor-${PROJECT_VERSION}-macOS-arm64")
  else()
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGE_FILE_NAME "frame-extractor-${PROJECT_VERSION}-macOS-development")
  endif()
else()
  set(CPACK_GENERATOR "TGZ")
  set(CPACK_PACKAGE_FILE_NAME "frame-extractor-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()
include(CPack)

if(FRAME_EXTRACTOR_BUILD_TESTS)
  include(CTest)
  find_package(Catch2 3 CONFIG REQUIRED)
  add_library(frame_extractor_test_support STATIC tests/fixture_support.cpp)
  target_compile_features(frame_extractor_test_support PUBLIC cxx_std_20)
  target_include_directories(frame_extractor_test_support PUBLIC tests)
  target_compile_definitions(frame_extractor_test_support
    PRIVATE FRAME_EXTRACTOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  frame_extractor_set_warnings(frame_extractor_test_support)

  add_executable(frame_extractor_tests
    tests/test_config.cpp
    tests/test_decoder.cpp
    tests/test_extraction_run.cpp
    tests/test_image_processing.cpp
    tests/test_output.cpp
    tests/test_processor.cpp
    tests/test_tracking.cpp
  )
  target_link_libraries(frame_extractor_tests
    PRIVATE
      frame_extractor::core
      frame_extractor_test_support
      Catch2::Catch2WithMain
  )
  target_include_directories(frame_extractor_tests PRIVATE src/core)
  target_compile_definitions(frame_extractor_tests
    PRIVATE FRAME_EXTRACTOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  frame_extractor_set_warnings(frame_extractor_tests)

  add_executable(frame_extractor_fixture_tool tests/fixture_tool.cpp)
  target_link_libraries(frame_extractor_fixture_tool PRIVATE frame_extractor_test_support)
  frame_extractor_set_warnings(frame_extractor_fixture_tool)

  include(Catch)
  catch_discover_tests(frame_extractor_tests)
  if(FRAME_EXTRACTOR_BUILD_GUI)
    add_executable(frame_extractor_gui_tests
      tests/test_gui_model.cpp
      tests/test_app_preferences.cpp
      tests/test_extraction_controller.cpp
      tests/test_manual_frame_exporter.cpp
      tests/test_platform.cpp
      tests/test_project_state.cpp
      tests/test_video_scrubber.cpp
    )
    target_include_directories(frame_extractor_gui_tests PRIVATE src/gui)
    target_link_libraries(frame_extractor_gui_tests
      PRIVATE
        frame_extractor_gui_support
        frame_extractor_test_support
        Catch2::Catch2WithMain
    )
    target_compile_definitions(frame_extractor_gui_tests
      PRIVATE FRAME_EXTRACTOR_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    frame_extractor_set_warnings(frame_extractor_gui_tests)
    catch_discover_tests(frame_extractor_gui_tests)
    add_test(NAME gui_version COMMAND frame_extractor_gui --version)
    if(FRAME_EXTRACTOR_GUI_SMOKE_TEST)
      add_test(NAME gui_startup
        COMMAND ${CMAKE_COMMAND}
          -DGUI=$<TARGET_FILE:frame_extractor_gui>
          -DFIXTURE_TOOL=$<TARGET_FILE:frame_extractor_fixture_tool>
          -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
          -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/gui-smoke
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/gui_smoke.cmake)
      set_tests_properties(gui_startup PROPERTIES TIMEOUT 75 LABELS "gui;smoke")
    endif()
  endif()
  add_test(NAME cli_help COMMAND frame_extractor_cli --help)
  add_test(NAME cli_version COMMAND frame_extractor_cli --version)
  add_test(NAME cli_rejects_missing_input COMMAND frame_extractor_cli)
  set_tests_properties(cli_rejects_missing_input PROPERTIES WILL_FAIL TRUE)
  add_test(NAME cli_rejects_zero_limit COMMAND frame_extractor_cli input.mov --max-frames 0)
  set_tests_properties(cli_rejects_zero_limit PROPERTIES WILL_FAIL TRUE)
  add_test(
    NAME cli_rejects_conflicting_limits
    COMMAND frame_extractor_cli input.mov --max-frames 2 --duration-frames 3
  )
  set_tests_properties(cli_rejects_conflicting_limits PROPERTIES WILL_FAIL TRUE)
  add_test(
    NAME cli_end_to_end
    COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:frame_extractor_cli>
      -DFIXTURE_TOOL=$<TARGET_FILE:frame_extractor_fixture_tool>
      -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-e2e
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_e2e.cmake
  )
  add_test(
    NAME selection_regression
    COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:frame_extractor_cli>
      -DFIXTURE_TOOL=$<TARGET_FILE:frame_extractor_fixture_tool>
      -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/selection-regression
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/selection_regression.cmake
  )
  set_tests_properties(selection_regression PROPERTIES
    TIMEOUT 45
    LABELS "regression;integration"
  )
endif()
