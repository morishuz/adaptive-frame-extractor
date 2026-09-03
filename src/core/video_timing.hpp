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

}  // namespace frame_extractor::detail
