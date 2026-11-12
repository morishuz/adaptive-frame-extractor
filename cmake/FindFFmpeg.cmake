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

# libavfilter is optional. When present on Linux, the decoder can shrink CUDA
# frames on the GPU (scale_cuda) before copying them to system memory. Only
# look next to the libavcodec already found so two FFmpeg installs never mix.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND FFmpeg_AVCODEC_LIBRARY AND FFmpeg_INCLUDE_DIR)
  get_filename_component(_ffmpeg_library_dir "${FFmpeg_AVCODEC_LIBRARY}" DIRECTORY)
  find_library(FFmpeg_AVFILTER_LIBRARY
    NAMES avfilter
    PATHS "${_ffmpeg_library_dir}"
    NO_DEFAULT_PATH
  )
  find_path(FFmpeg_AVFILTER_INCLUDE_DIR
    NAMES libavfilter/buffersrc.h
    PATHS "${FFmpeg_INCLUDE_DIR}"
    NO_DEFAULT_PATH
  )
endif()

if(FFmpeg_FOUND AND NOT TARGET FFmpeg::FFmpeg)
  add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
  set(_ffmpeg_link_libraries
    "${FFmpeg_AVFORMAT_LIBRARY};${FFmpeg_AVCODEC_LIBRARY};${FFmpeg_SWSCALE_LIBRARY};${FFmpeg_AVUTIL_LIBRARY}")
  if(FFmpeg_AVFILTER_LIBRARY AND FFmpeg_AVFILTER_INCLUDE_DIR)
    set(_ffmpeg_link_libraries "${FFmpeg_AVFILTER_LIBRARY};${_ffmpeg_link_libraries}")
    set_target_properties(FFmpeg::FFmpeg PROPERTIES
      INTERFACE_COMPILE_DEFINITIONS FRAME_EXTRACTOR_HAVE_AVFILTER=1)
    message(STATUS "FFmpeg libavfilter found: GPU analysis downscaling enabled")
  endif()
  set_target_properties(FFmpeg::FFmpeg PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${_ffmpeg_link_libraries}"
  )
endif()

mark_as_advanced(
  FFmpeg_INCLUDE_DIR
  FFmpeg_AVCODEC_LIBRARY
  FFmpeg_AVFORMAT_LIBRARY
  FFmpeg_AVUTIL_LIBRARY
  FFmpeg_SWSCALE_LIBRARY
  FFmpeg_AVFILTER_LIBRARY
  FFmpeg_AVFILTER_INCLUDE_DIR
)

