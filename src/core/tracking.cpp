#include "frame_extractor/tracking.hpp"

#include "flow_sampling.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace frame_extractor {
namespace {

std::vector<float> gridAxis(int length, int step, int margin) {
  if (length <= 0) {
    throw std::invalid_argument("image dimensions must be positive");
  }
  const int start = std::min(std::max(margin, 0), std::max(length - 1, 0));
  const int stop = std::max(start + 1, length - std::max(margin, 0));
  std::vector<float> axis;
  for (int value = start; value < stop; value += std::max(1, step)) {
    axis.push_back(static_cast<float>(value));
  }
  if (axis.empty()) {
    axis.push_back(std::max(0.0F, static_cast<float>(length - 1) / 2.0F));
  }
  return axis;
}

bool finite(Point2f point) {
  return std::isfinite(point.x) && std::isfinite(point.y);
}

}  // namespace

TrackingState initializeTrackingState(int width, int height, const Config& config) {
  const auto xs = gridAxis(
      width,
      config.sampling.grid_step_analysis_px,
      config.sampling.min_margin_analysis_px);
  const auto ys = gridAxis(
      height,
      config.sampling.grid_step_analysis_px,
      config.sampling.min_margin_analysis_px);

  TrackingState state;
  state.origin_points.reserve(xs.size() * ys.size());
  for (const float y : ys) {
    for (const float x : xs) {
      state.origin_points.push_back(Point2f{x, y});
    }
  }
  state.current_points = state.origin_points;
  state.alive_mask.assign(state.origin_points.size(), std::uint8_t{1});
  return state;
}

std::vector<std::uint8_t> insideImage(
    std::span<const Point2f> points,
    int width,
    int height) {
  std::vector<std::uint8_t> mask(points.size(), std::uint8_t{0});
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto point = points[index];
    mask[index] = static_cast<std::uint8_t>(
        finite(point) && point.x >= 0.0F && point.x <= static_cast<float>(width - 1)
        && point.y >= 0.0F && point.y <= static_cast<float>(height - 1));
  }
  return mask;
}

std::vector<std::uint8_t> beyondLostBorder(
    std::span<const Point2f> points,
    int width,
    int height,
    double lost_border_px) {
  std::vector<std::uint8_t> mask(points.size(), std::uint8_t{0});
  const float border = static_cast<float>(lost_border_px);
  for (std::size_t index = 0; index < points.size(); ++index) {
    const auto point = points[index];
    mask[index] = static_cast<std::uint8_t>(
        !finite(point) || point.x < -border
        || point.x > static_cast<float>(width - 1) + border || point.y < -border
        || point.y > static_cast<float>(height - 1) + border);
  }
  return mask;
}

FlowStepDiagnostics detail::applySampledFlow(
    TrackingState& state,
    const detail::SampledFlow& sampled,
    int current_width,
    int current_height,
    const Config& config) {
  if (state.current_points.size() != state.origin_points.size()
      || state.alive_mask.size() != state.origin_points.size()
      || sampled.values.size() != state.origin_points.size()
      || sampled.valid_mask.size() != state.origin_points.size()) {
    throw std::invalid_argument("tracking state arrays must have equal lengths");
  }

  const float maximum_step = static_cast<float>(config.max_step_norm_analysis_px);
  for (std::size_t index = 0; index < state.current_points.size(); ++index) {
    if (sampled.valid_mask[index] != 0U) {
      auto step = sampled.values[index];
      if (maximum_step > 0.0F) {
        const float norm = std::hypot(step.x, step.y);
        if (norm > maximum_step) {
          const float scale = maximum_step / std::max(norm, 1.0e-6F);
          step.x *= scale;
          step.y *= scale;
        }
      }
      state.current_points[index].x += step.x;
      state.current_points[index].y += step.y;
    }
  }

  auto in_bounds = insideImage(state.current_points, current_width, current_height);
  const auto lost = beyondLostBorder(
      state.current_points,
      current_width,
      current_height,
      config.sampling.lost_border_analysis_px);
  for (std::size_t index = 0; index < state.alive_mask.size(); ++index) {
    state.alive_mask[index] = static_cast<std::uint8_t>(
        state.alive_mask[index] != 0U && lost[index] == 0U);
  }
  return FlowStepDiagnostics{std::move(in_bounds)};
}

FrameScores computeFrameScores(
    const TrackingState& state,
    const FlowStepDiagnostics& diagnostics,
    std::int64_t frame_index,
    double timestamp_sec,
    const Config& config) {
  if (state.current_points.size() != state.origin_points.size()
      || state.alive_mask.size() != state.origin_points.size()
      || diagnostics.in_bounds_mask.size() != state.origin_points.size()) {
    throw std::invalid_argument("tracking and diagnostics arrays must have equal lengths");
  }

  std::vector<double> scored_displacements;
  std::size_t in_bounds_points = 0;
  for (std::size_t index = 0; index < state.origin_points.size(); ++index) {
    const bool scored = state.alive_mask[index] != 0U
        && diagnostics.in_bounds_mask[index] != 0U;
    if (!scored) {
      continue;
    }
    ++in_bounds_points;
    const float dx = state.current_points[index].x - state.origin_points[index].x;
    const float dy = state.current_points[index].y - state.origin_points[index].y;
    const float displacement = std::hypot(dx, dy);
    if (std::isfinite(displacement)) {
      scored_displacements.push_back(static_cast<double>(displacement));
    }
  }

  const auto denominator = std::max<std::size_t>(state.origin_points.size(), 1U);
  return FrameScores{
      frame_index,
      timestamp_sec,
      linearPercentile(scored_displacements, config.scoring.percentile),
      in_bounds_points,
      static_cast<double>(in_bounds_points) / static_cast<double>(denominator)};
}

TriggerDecision decideTrigger(
    const FrameScores& scores,
    int frames_since_keyframe,
    const TriggerConfig& config) {
  const bool trigger_allowed = frames_since_keyframe >= config.min_frames_since_keyframe;
  std::vector<std::string> reasons;
  if (trigger_allowed && scores.global_score >= config.main_threshold_analysis_px) {
    reasons.emplace_back("main");
  }
  if (trigger_allowed && scores.in_bounds_ratio < config.min_in_bounds_ratio) {
    reasons.emplace_back("in_bounds");
  }
  if (config.max_frames_since_keyframe > 0
      && frames_since_keyframe >= config.max_frames_since_keyframe) {
    reasons.emplace_back("interval");
  }

  std::string reason = "none";
  if (!reasons.empty()) {
    reason.clear();
    for (const auto& item : reasons) {
      if (!reason.empty()) {
        reason += "+";
      }
      reason += item;
    }
  }
  return TriggerDecision{!reasons.empty(), std::move(reason), frames_since_keyframe};
}

double linearPercentile(std::span<const double> values, double percentile) {
  if (percentile < 0.0 || percentile > 100.0 || !std::isfinite(percentile)) {
    throw std::invalid_argument("percentile must be in [0, 100]");
  }
  std::vector<double> finite_values;
  finite_values.reserve(values.size());
  std::copy_if(values.begin(), values.end(), std::back_inserter(finite_values), [](double value) {
    return std::isfinite(value);
  });
  if (finite_values.empty()) {
    return 0.0;
  }
  std::sort(finite_values.begin(), finite_values.end());
  const double rank = (percentile / 100.0) * static_cast<double>(finite_values.size() - 1U);
  const auto lower = static_cast<std::size_t>(std::floor(rank));
  const auto upper = static_cast<std::size_t>(std::ceil(rank));
  const double fraction = rank - static_cast<double>(lower);
  return finite_values[lower] * (1.0 - fraction) + finite_values[upper] * fraction;
}

std::string TriggerDecision::displayReason() const {
  if (!triggered) {
    return {};
  }
  std::string output;
  std::size_t start = 0;
  while (start <= reason.size()) {
    const auto end = reason.find('+', start);
    const auto token = reason.substr(start, end == std::string::npos ? end : end - start);
    if (!output.empty()) {
      output += "+";
    }
    if (token == "main") {
      output += "motion";
    } else if (token == "in_bounds") {
      output += "low_points";
    } else {
      output += token;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1U;
  }
  return output;
}

}  // namespace frame_extractor
