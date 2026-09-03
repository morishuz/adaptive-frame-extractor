find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_FFMPEG QUIET libavcodec libavformat libavutil libswscale)
endif()

find_path(FFmpeg_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  HINTS ${PC_FFMPEG_INCLUDE_DIRS}
)
find_library(FFmpeg_AVCODEC_LIBRARY
  NAMES avcodec
  HINTS ${PC_FFMPEG_LIBRARY_DIRS}
)
find_library(FFmpeg_AVFORMAT_LIBRARY
  NAMES avformat
  HINTS ${PC_FFMPEG_LIBRARY_DIRS}
)
find_library(FFmpeg_AVUTIL_LIBRARY
  NAMES avutil
  HINTS ${PC_FFMPEG_LIBRARY_DIRS}
)
find_library(FFmpeg_SWSCALE_LIBRARY
  NAMES swscale
  HINTS ${PC_FFMPEG_LIBRARY_DIRS}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg
  REQUIRED_VARS
    FFmpeg_INCLUDE_DIR
    FFmpeg_AVCODEC_LIBRARY
    FFmpeg_AVFORMAT_LIBRARY
    FFmpeg_AVUTIL_LIBRARY
    FFmpeg_SWSCALE_LIBRARY
)

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
  add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
  set_target_properties(FFmpeg::FFmpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES
      "${FFmpeg_AVFORMAT_LIBRARY};${FFmpeg_AVCODEC_LIBRARY};${FFmpeg_SWSCALE_LIBRARY};${FFmpeg_AVUTIL_LIBRARY}"
  )
endif()

mark_as_advanced(
  FFmpeg_INCLUDE_DIR
  FFmpeg_AVCODEC_LIBRARY
  FFmpeg_AVFORMAT_LIBRARY
  FFmpeg_AVUTIL_LIBRARY
  FFmpeg_SWSCALE_LIBRARY
)

