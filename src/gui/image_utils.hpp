#pragma once

#include <opencv2/core.hpp>

namespace frame_extractor::gui {

[[nodiscard]] cv::Mat boundedCopy(
    const cv::Mat& source,
    int maximum_width,
    int maximum_height);

}  // namespace frame_extractor::gui
