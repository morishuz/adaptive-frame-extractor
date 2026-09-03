#include "image_utils.hpp"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>

namespace frame_extractor::gui {

cv::Mat boundedCopy(const cv::Mat& source, int maximum_width, int maximum_height) {
  if (source.empty()) {
    return {};
  }
  const double scale = std::min({
      1.0,
      static_cast<double>(maximum_width) / source.cols,
      static_cast<double>(maximum_height) / source.rows});
  if (scale >= 1.0) {
    return source.clone();
  }
  cv::Mat resized;
  cv::resize(
      source,
      resized,
      cv::Size{
          std::max(1, static_cast<int>(std::lround(source.cols * scale))),
          std::max(1, static_cast<int>(std::lround(source.rows * scale)))},
      0.0,
      0.0,
      cv::INTER_AREA);
  return resized;
}

}  // namespace frame_extractor::gui
