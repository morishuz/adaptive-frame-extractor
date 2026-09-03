#pragma once

#include "frame_extractor/config.hpp"
#include "frame_extractor/tracking.hpp"

#include <opencv2/core.hpp>
#include <opencv2/video/tracking.hpp>

namespace frame_extractor {

struct TrackingStepTimings {
  double dense_flow_seconds{};
  double point_sampling_seconds{};
};

[[nodiscard]] cv::Mat ensureGray(const cv::Mat& frame);
[[nodiscard]] cv::Size analysisFrameSize(cv::Size source_size, int target_area_px);
[[nodiscard]] cv::Mat resizeForAnalysis(const cv::Mat& frame, int target_area_px);
[[nodiscard]] cv::Ptr<cv::DISOpticalFlow> createDisFlow(const DisConfig& config);
[[nodiscard]] cv::Mat computeForwardFlow(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    cv::DISOpticalFlow& solver);
[[nodiscard]] FlowStepDiagnostics stepTracking(
    TrackingState& state,
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    cv::DISOpticalFlow& solver,
    const Config& config,
    TrackingStepTimings* timings = nullptr);

}  // namespace frame_extractor
