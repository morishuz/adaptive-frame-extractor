#pragma once

#include "frame_extractor/config.hpp"

#include <opencv2/core/mat.hpp>

#include <filesystem>
#include <vector>

namespace frame_extractor::detail {

struct ImageEncodingSettings {
  int jpeg_quality{95};
  int png_compression_level{1};
};

// TIFF output is always 8-bit with lossless LZW compression. LZW is built into
// libtiff, so it needs no optional codec, and every common TIFF reader opens it.
inline constexpr int tiffLzwCompression = 5;

class ImageFileWriter final {
 public:
  explicit ImageFileWriter(
      ImageFormat format,
      ImageEncodingSettings settings = {});

  void writeAtomically(
      const std::filesystem::path& path,
      const cv::Mat& frame_bgr) const;

 private:
  ImageFormat format_;
  std::vector<int> encoding_parameters_;
};

}  // namespace frame_extractor::detail
