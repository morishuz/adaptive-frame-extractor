#pragma once

#include "frame_extractor/decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace frame_extractor::detail {

[[nodiscard]] inline bool hasStableSeekFrameIndices(
    Rational average_rate,
    Rational nominal_rate,
    std::optional<std::int64_t> reported_frame_count,
    std::optional<double> duration_seconds) {
  const auto validRate = [](Rational rate) {
    return rate.numerator > 0 && rate.denominator > 0;
  };
  if (!validRate(average_rate) || !validRate(nominal_rate)) {
    return false;
  }

  const double average = static_cast<double>(average_rate.numerator)
      / static_cast<double>(average_rate.denominator);
  const double nominal = static_cast<double>(nominal_rate.numerator)
      / static_cast<double>(nominal_rate.denominator);
  if (std::abs(average - nominal)
      <= 1.0e-12 * std::max(average, nominal)) {
    return true;
  }
  if (!reported_frame_count || *reported_frame_count <= 0
      || !duration_seconds || !std::isfinite(*duration_seconds)
      || *duration_seconds <= 0.0) {
    return false;
  }

  // Container rounding can make an effectively constant-rate stream report a
  // slightly different average rate. Accept it only when neither rate nor
  // duration can move a recovered ordinal across a half-frame boundary.
  constexpr double maximum_frame_drift = 0.5;
  const double rate_drift_frames =
      std::abs(average - nominal) * *duration_seconds;
  const double count_drift_frames = std::abs(
      nominal * *duration_seconds
      - static_cast<double>(*reported_frame_count));
  return rate_drift_frames < maximum_frame_drift
      && count_drift_frames < maximum_frame_drift;
}

// Number of exact 2x scale_cuda halvings from source to target, or nullopt
// when the GPU result could differ from the CPU path. At exactly 2x, bilinear
// scale_cuda and OpenCV INTER_AREA both compute (a + b + c + d + 2) >> 2.
// Chaining halvings rounds at every stage, while the CPU resizes in one step
// and rounds once. Each halving adds two fractional bits, so chaining stays
// exact only while the low padding bits of the luma sample absorb them
// (P010 has 6, so up to 3 halvings). 8-bit formats have none, so they get a
// single halving.
[[nodiscard]] inline std::optional<int> exactGpuHalvings(
    cv::Size source, cv::Size target, int luma_padding_bits) {
  int stages = 0;
  cv::Size current = source;
  while (current != target && current.width % 2 == 0 && current.height % 2 == 0
         && current.width / 2 >= target.width && current.height / 2 >= target.height) {
    current = {current.width / 2, current.height / 2};
    ++stages;
  }
  if (current != target || stages == 0) {
    return std::nullopt;
  }
  if (stages > std::max(1, luma_padding_bits / 2)) {
    return std::nullopt;
  }
  return stages;
}

}  // namespace frame_extractor::detail
