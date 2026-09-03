#pragma once

#include "frame_extractor/tracking.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace frame_extractor::detail {

struct SampledFlow {
  std::vector<Point2f> values{};
  std::vector<std::uint8_t> valid_mask{};
};

[[nodiscard]] FlowStepDiagnostics applySampledFlow(
    TrackingState& state,
    const SampledFlow& sampled,
    int current_width,
    int current_height,
    const Config& config);

template <typename FlowAt>
SampledFlow sampleBilinearFlow(
    int width,
    int height,
    std::span<const Point2f> points,
    FlowAt&& flow_at) {
  SampledFlow sampled{
      std::vector<Point2f>(points.size()),
      std::vector<std::uint8_t>(points.size(), std::uint8_t{0})};
  if (width == 0 || height == 0) {
    return sampled;
  }

  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto point = points[index];
    if (!std::isfinite(point.x) || !std::isfinite(point.y)
        || point.x < 0.0F || point.y < 0.0F
        || point.x > static_cast<float>(width - 1)
        || point.y > static_cast<float>(height - 1)) {
      continue;
    }

    const int x0 = static_cast<int>(std::floor(point.x));
    const int y0 = static_cast<int>(std::floor(point.y));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float dx = point.x - static_cast<float>(x0);
    const float dy = point.y - static_cast<float>(y0);
    const auto q00 = flow_at(x0, y0);
    const auto q10 = flow_at(x1, y0);
    const auto q01 = flow_at(x0, y1);
    const auto q11 = flow_at(x1, y1);

    const Point2f top{
        q00.x * (1.0F - dx) + q10.x * dx,
        q00.y * (1.0F - dx) + q10.y * dx};
    const Point2f bottom{
        q01.x * (1.0F - dx) + q11.x * dx,
        q01.y * (1.0F - dx) + q11.y * dx};
    sampled.values[index] = Point2f{
        top.x * (1.0F - dy) + bottom.x * dy,
        top.y * (1.0F - dy) + bottom.y * dy};
    sampled.valid_mask[index] = std::uint8_t{1};
  }
  return sampled;
}

}  // namespace frame_extractor::detail
