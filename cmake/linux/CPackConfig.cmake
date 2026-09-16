# CPack reads this once for each generator; the portable archive keeps its layout.
if(CPACK_GENERATOR STREQUAL "DEB")
  set(CPACK_PACKAGING_INSTALL_PREFIX "/opt/frame-extractor")
endif()
