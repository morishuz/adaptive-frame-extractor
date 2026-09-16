if(NOT CPACK_GENERATOR STREQUAL "DEB")
  return()
endif()

# Use a private prefix for bundled libraries and binary-relative resources.
# Only the launchers and desktop integration belong in the global directories.
set(_root "${CPACK_TEMPORARY_DIRECTORY}")
set(_app "${_root}/opt/frame-extractor")
file(MAKE_DIRECTORY "${_root}/usr/bin")
foreach(_binary frame-extractor frame-extractor-gui)
  if(EXISTS "${_app}/bin/${_binary}")
    file(CREATE_LINK "../../opt/frame-extractor/bin/${_binary}"
      "${_root}/usr/bin/${_binary}" SYMBOLIC)
  endif()
endforeach()
if(EXISTS "${_app}/bin/frame-extractor-gui")
  file(MAKE_DIRECTORY "${_root}/usr/share")
  file(RENAME "${_app}/share/applications" "${_root}/usr/share/applications")
  file(RENAME "${_app}/share/icons" "${_root}/usr/share/icons")
endif()
# apt/dpkg installs the desktop files; the portable registration helper is unused.
file(REMOVE "${_app}/bin/install-desktop-integration.py")
