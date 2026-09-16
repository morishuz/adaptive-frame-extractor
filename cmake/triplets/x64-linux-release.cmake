set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_BUILD_TYPE release)

# GNOME/Wayland needs client-side decorations. Changing the triplet also
# invalidates SDL binaries cached before libdecor development headers were added.
if(PORT STREQUAL "sdl3")
  list(APPEND VCPKG_CMAKE_CONFIGURE_OPTIONS -DSDL_WAYLAND_LIBDECOR=ON)
endif()
