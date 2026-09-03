#include "frame_extractor/image_processing.hpp"

#include "flow_sampling.hpp"

#include <opencv2/imgproc.hpp>
#include <opencv2/video.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>

namespace frame_extractor {

cv::Mat ensureGray(const cv::Mat& frame) {
  if (frame.empty()) {
    throw std::invalid_argument("frame must not be empty");
  }
  if (frame.channels() == 1) {
    return frame;
  }
  if (frame.channels() == 3) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    return gray;
  }
  throw std::invalid_argument("frame must be grayscale or three-channel BGR");
}

cv::Size analysisFrameSize(cv::Size source_size, int target_area_px) {
  if (source_size.width <= 0 || source_size.height <= 0) {
    throw std::invalid_argument("source dimensions must be positive");
  }
  if (target_area_px <= 0) {
    throw std::invalid_argument("target analysis area must be positive");
  }

  const double source_area = static_cast<double>(source_size.width)
      * static_cast<double>(source_size.height);
  if (source_area <= static_cast<double>(target_area_px)) {
    return source_size;
  }

  const double scale = std::sqrt(static_cast<double>(target_area_px) / source_area);
  return {
      std::clamp(
          static_cast<int>(std::lround(static_cast<double>(source_size.width) * scale)),
          1,
          source_size.width),
      std::clamp(
          static_cast<int>(std::lround(static_cast<double>(source_size.height) * scale)),
          1,
          source_size.height)};
}

cv::Mat resizeForAnalysis(const cv::Mat& frame, int target_area_px) {
  if (frame.empty()) {
    throw std::invalid_argument("frame must not be empty");
  }
  const auto target_size = analysisFrameSize(frame.size(), target_area_px);
  if (target_size == frame.size()) {
    return frame;
  }

  cv::Mat output;
  cv::resize(frame, output, target_size, 0.0, 0.0, cv::INTER_AREA);
  return output;
}

cv::Ptr<cv::DISOpticalFlow> createDisFlow(const DisConfig& config) {
  int preset = cv::DISOpticalFlow::PRESET_ULTRAFAST;
  switch (config.preset) {
    case DisPreset::ultrafast:
      preset = cv::DISOpticalFlow::PRESET_ULTRAFAST;
      break;
    case DisPreset::fast:
      preset = cv::DISOpticalFlow::PRESET_FAST;
      break;
    case DisPreset::medium:
      preset = cv::DISOpticalFlow::PRESET_MEDIUM;
      break;
  }
  auto solver = cv::DISOpticalFlow::create(preset);
  solver->setFinestScale(config.finest_scale);
  solver->setPatchSize(config.patch_size);
  solver->setPatchStride(config.patch_stride);
  solver->setGradientDescentIterations(config.gradient_descent_iterations);
  solver->setVariationalRefinementIterations(config.variational_refinement_iterations);
  solver->setUseSpatialPropagation(config.use_spatial_propagation);
  return solver;
}

cv::Mat computeForwardFlow(
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    cv::DISOpticalFlow& solver) {
  if (previous_gray.empty() || current_gray.empty()) {
    throw std::invalid_argument("DIS input frames must not be empty");
  }
  if (previous_gray.type() != CV_8UC1 || current_gray.type() != CV_8UC1) {
    throw std::invalid_argument("DIS input frames must be 8-bit grayscale");
  }
  if (previous_gray.size() != current_gray.size()) {
    throw std::invalid_argument("DIS input frames must have equal dimensions");
  }
  cv::Mat flow;
  solver.calc(previous_gray, current_gray, flow);
  if (flow.type() != CV_32FC2 || flow.size() != previous_gray.size()) {
    throw std::runtime_error("DIS returned an unexpected flow field");
  }
  return flow;
}

FlowStepDiagnostics stepTracking(
    TrackingState& state,
    const cv::Mat& previous_gray,
    const cv::Mat& current_gray,
    cv::DISOpticalFlow& solver,
    const Config& config,
    TrackingStepTimings* timings) {
  const auto dense_flow_started = std::chrono::steady_clock::now();
  const auto flow = computeForwardFlow(previous_gray, current_gray, solver);
  const double dense_flow_seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - dense_flow_started).count();
  const auto point_sampling_started = std::chrono::steady_clock::now();
  auto diagnostics = detail::applySampledFlow(
      state,
      detail::sampleBilinearFlow(
          flow.cols,
          flow.rows,
          state.current_points,
          [&flow](int x, int y) {
            const auto value = flow.at<cv::Vec2f>(y, x);
            return Point2f{value[0], value[1]};
          }),
      current_gray.cols,
      current_gray.rows,
      config);
  if (timings != nullptr) {
    timings->dense_flow_seconds = dense_flow_seconds;
    timings->point_sampling_seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - point_sampling_started).count();
  }
  return diagnostics;
}

}  // namespace frame_extractor
