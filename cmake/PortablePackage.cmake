set(_runtime_directories "${FRAME_EXTRACTOR_VCPKG_PREFIX}/bin" "${FRAME_EXTRACTOR_VCPKG_PREFIX}/lib")
if(MSVC)
  # Ship the redistributable runtime, not arbitrary system DLLs. UCRT itself is
  # part of the Windows 10+ baseline. Search the redist directory even if this
  # build machine does not have the runtime installed in System32.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
  include(InstallRequiredSystemLibraries)
  foreach(_runtime IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
    get_filename_component(_directory "${_runtime}" DIRECTORY)
    list(APPEND _runtime_directories "${_directory}")
  endforeach()
  list(REMOVE_DUPLICATES _runtime_directories)
endif()

# Scan the real executable imports, including FFmpeg's transitive dependencies.
# TARGET_RUNTIME_DLLS alone cannot discover libraries from our FFmpeg interface target.
install(RUNTIME_DEPENDENCY_SET frame_extractor_runtime
  DIRECTORIES ${_runtime_directories}
  PRE_EXCLUDE_REGEXES "api-ms-.*" "ext-ms-.*"
  POST_EXCLUDE_REGEXES
    ".*[\\\\/][Ww][Ii][Nn][Dd][Oo][Ww][Ss][\\\\/][Ss][Yy][Ss][Tt][Ee][Mm]32[\\\\/].*"
    "^/lib/" "^/lib64/" "^/usr/lib/" "^/usr/lib64/"
  RUNTIME DESTINATION bin
  LIBRARY DESTINATION lib
)

configure_file("${CMAKE_CURRENT_LIST_DIR}/FixupPortablePackage.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/FixupPortablePackage.cmake" @ONLY)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/FixupPortablePackage.cmake")
