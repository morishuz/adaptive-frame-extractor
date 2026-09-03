#pragma once

#include "frame_extractor/tracking.hpp"

#include <opencv2/core.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace frame_extractor::gui {

inline constexpr std::size_t metric_history_capacity = 180U;
inline constexpr std::size_t thumbnail_history_capacity = 96U;

struct RegionLengthEstimate {
  double seconds{};
  std::optional<std::int64_t> inclusive_frames;
};

[[nodiscard]] inline std::optional<RegionLengthEstimate> estimateRegionLength(
    double in_seconds,
    double out_seconds,
    std::optional<double> average_fps) {
  const double seconds = out_seconds - in_seconds;
  if (!std::isfinite(seconds) || seconds <= 0.0) {
    return std::nullopt;
  }

  std::optional<std::int64_t> inclusive_frames;
  if (average_fps && std::isfinite(*average_fps) && *average_fps > 0.0) {
    inclusive_frames = std::llround(seconds * *average_fps) + 1;
  }
  return RegionLengthEstimate{seconds, inclusive_frames};
}

template <typename T>
void trimToMostRecent(std::vector<T>& values, std::size_t capacity) {
  if (values.size() > capacity) {
    values.erase(
        values.begin(),
        values.end() - static_cast<std::ptrdiff_t>(capacity));
  }
}

enum class RunPhase { idle, running, cancelling, complete, cancelled, failed };

[[nodiscard]] inline bool isActive(RunPhase phase) {
  return phase == RunPhase::running || phase == RunPhase::cancelling;
}

[[nodiscard]] inline bool isTerminal(RunPhase phase) {
  return phase == RunPhase::complete || phase == RunPhase::cancelled
      || phase == RunPhase::failed;
}

struct PendingImage {
  cv::Mat bgr;
  std::int64_t decoded_frame_index{};
  double timestamp_seconds{};
  int frames_since_keyframe{};
  std::vector<Point2f> normalized_tracking_points;
};

struct PendingThumbnail {
  cv::Mat bgr;
  std::size_t keyframe_index{};
  std::int64_t decoded_frame_index{};
};

struct MetricSample {
  std::int64_t frame_index{};
  double motion_score{};
  double in_bounds_ratio{1.0};
  bool triggered{};
  std::size_t region_index{};
};

struct RunOutcome {
  double runtime_seconds{};
  std::size_t processed_frames{};
  std::size_t selected_keyframes{};
  bool outputs_finalized{};

  [[nodiscard]] std::optional<double> meanFramesPerKeyframe() const {
    if (selected_keyframes == 0U) {
      return std::nullopt;
    }
    return static_cast<double>(processed_frames)
        / static_cast<double>(selected_keyframes);
  }
};

inline void appendMetricSamples(
    std::vector<MetricSample>& history,
    std::span<const MetricSample> samples) {
  for (const auto& sample : samples) {
    if (!history.empty() && history.back().region_index != sample.region_index) {
      history.clear();
    }
    history.push_back(sample);
  }
  trimToMostRecent(history, metric_history_capacity);
}

struct RunSnapshot {
  RunPhase phase{RunPhase::idle};
  std::string status;
  std::string error;
  std::string run_directory;
  std::optional<std::size_t> total_frames;
  std::size_t processed_frames{};
  std::size_t selected_keyframes{};
  std::optional<RunOutcome> outcome;
  std::optional<double> progress_timestamp_seconds;
  std::optional<PendingImage> preview;
  std::vector<MetricSample> metric_samples;
  std::vector<PendingThumbnail> thumbnails;
};

}  // namespace frame_extractor::gui
