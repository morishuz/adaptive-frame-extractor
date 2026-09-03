#pragma once

#include "model.hpp"

#include "frame_extractor/config.hpp"
#include "frame_extractor/processor.hpp"

#include "imgui.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace frame_extractor::gui {

[[nodiscard]] const char* comparisonSymbol(double value, double threshold);

enum class RunResultAction { none, open_run_folder, open_summary };

[[nodiscard]] std::string runResultHeading(const RunSnapshot& snapshot);

[[nodiscard]] RunResultAction drawRunResultPanel(const RunSnapshot& snapshot);

struct TimelineView {
  double duration_seconds{};
  double playhead_seconds{};
  std::span<const TimeRange> regions;
  std::optional<double> pending_in_seconds;
  std::optional<double> progress_seconds;
  bool interactive{};
};

[[nodiscard]] std::optional<double> drawTimeline(
    const char* id,
    const TimelineView& view,
    ImVec2 size);

void drawMetricPlot(
    const char* id,
    const char* title,
    const std::vector<MetricSample>& history,
    double MetricSample::* value_member,
    double threshold,
    const std::array<std::uint8_t, 3>& value_color,
    const VisualizationConfig& visualization,
    ImVec2 size,
    std::optional<double> fixed_max = std::nullopt);

}  // namespace frame_extractor::gui
