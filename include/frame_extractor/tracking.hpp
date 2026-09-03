#pragma once

#include "frame_extractor/config.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace frame_extractor {

struct Point2f {
  float x{};
  float y{};

  bool operator==(const Point2f&) const = default;
};

struct TrackingState {
  std::vector<Point2f> origin_points{};
  std::vector<Point2f> current_points{};
  std::vector<std::uint8_t> alive_mask{};
};

struct FlowStepDiagnostics {
  std::vector<std::uint8_t> in_bounds_mask{};
};

struct FrameScores {
  std::int64_t frame_index{};
  double timestamp_sec{};
  double global_score{};
  std::size_t in_bounds_points{};
  double in_bounds_ratio{};
};

struct TriggerDecision {
  bool triggered{};
  std::string reason{"none"};
  int frames_since_keyframe{};

  [[nodiscard]] std::string displayReason() const;
};

[[nodiscard]] TrackingState initializeTrackingState(int width, int height, const Config& config);
[[nodiscard]] std::vector<std::uint8_t> insideImage(std::span<const Point2f> points, int width, int height);
[[nodiscard]] std::vector<std::uint8_t> beyondLostBorder(
    std::span<const Point2f> points,
    int width,
    int height,
    double lost_border_px);
[[nodiscard]] FrameScores computeFrameScores(
    const TrackingState& state,
    const FlowStepDiagnostics& diagnostics,
    std::int64_t frame_index,
    double timestamp_sec,
    const Config& config);
[[nodiscard]] TriggerDecision decideTrigger(
    const FrameScores& scores,
    int frames_since_keyframe,
    const TriggerConfig& config);
[[nodiscard]] double linearPercentile(std::span<const double> values, double percentile);

}  // namespace frame_extractor
