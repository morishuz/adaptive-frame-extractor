#include "frame_extractor/detail/image_writer.hpp"

#include "frame_extractor/detail/atomic_output_file.hpp"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace frame_extractor::detail {
namespace {

bool extensionMatches(
    const std::filesystem::path& path,
    ImageFormat format) {
  auto extension = path.extension().string();
  std::ranges::transform(extension, extension.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return format == ImageFormat::jpg
      ? extension == ".jpg" || extension == ".jpeg"
      : extension == ".png";
}

}  // namespace

ImageFileWriter::ImageFileWriter(
    ImageFormat format,
    ImageEncodingSettings settings)
    : format_{format},
      encoding_parameters_{format == ImageFormat::jpg
          ? std::vector<int>{cv::IMWRITE_JPEG_QUALITY, settings.jpeg_quality}
          : std::vector<int>{
                cv::IMWRITE_PNG_COMPRESSION,
                settings.png_compression_level}} {}

void ImageFileWriter::writeAtomically(
    const std::filesystem::path& path,
    const cv::Mat& frame_bgr) const {
  if (path.empty() || !extensionMatches(path, format_)) {
    throw std::invalid_argument(
        "image path extension does not match the selected format: "
        + path.string());
  }
  if (frame_bgr.empty()) {
    throw std::invalid_argument("image must not be empty");
  }

  std::error_code error;
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      throw std::runtime_error(
          "Cannot create image output directory: " + error.message());
    }
  }

  AtomicOutputFile output_file{path};
  try {
    std::vector<unsigned char> encoded;
    if (!cv::imencode(path.extension().string(), frame_bgr, encoded, encoding_parameters_)) {
      throw std::runtime_error("encoder returned no output");
    }
    std::ofstream output{output_file.temporaryPath(), std::ios::binary};
    output.write(
        reinterpret_cast<const char*>(encoded.data()),
        static_cast<std::streamsize>(encoded.size()));
    output.close();
    if (!output.good()) {
      throw std::runtime_error("cannot write encoded image data");
    }
  } catch (const std::exception& error) {
    throw std::runtime_error(
        "Failed to write image " + path.string() + ": " + error.what());
  }
  output_file.publish();
}

}  // namespace frame_extractor::detail
