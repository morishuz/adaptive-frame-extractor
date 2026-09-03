#include "widgets.hpp"

#include "platform.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>

namespace frame_extractor::gui {
namespace {

ImU32 colorFromRgb(const std::array<std::uint8_t, 3>& color) {
  return IM_COL32(color[0], color[1], color[2], 255);
}

void drawDashedHorizontalLine(
    ImDrawList* draw_list,
    float start_x,
    float end_x,
    float y,
    ImU32 color) {
  constexpr float dash_length = 6.0F;
  constexpr float gap_length = 4.0F;
  for (float x = start_x; x < end_x; x += dash_length + gap_length) {
    draw_list->AddLine(
        ImVec2{x, y},
        ImVec2{std::min(x + dash_length, end_x), y},
        color);
  }
}

}  // namespace

const char* comparisonSymbol(double value, double threshold) {
  const double tolerance = 1.0e-5 * std::max({1.0, std::abs(value), std::abs(threshold)});
  if (std::abs(value - threshold) <= tolerance) {
    return "=";
  }
  return value < threshold ? "<" : ">";
}

std::string runResultHeading(const RunSnapshot& snapshot) {
  if (snapshot.phase != RunPhase::complete || snapshot.run_directory.empty()) {
    return snapshot.status;
  }

  const std::filesystem::path run_directory = pathFromUtf8(snapshot.run_directory);
  auto run_name = run_directory.filename().string();
  if (run_name.empty()) {
    run_name = run_directory.parent_path().filename().string();
  }
  if (run_name.empty()) {
    return snapshot.status;
  }

  std::string heading = snapshot.status;
  if (heading.ends_with('.')) {
    heading.pop_back();
  }
  return heading + ": " + run_name;
}

RunResultAction drawRunResultPanel(const RunSnapshot& snapshot) {
  ImGui::SeparatorText("Run results");
  const ImVec4 status_color = snapshot.phase == RunPhase::complete
      ? ImVec4{0.35F, 0.85F, 0.5F, 1.0F}
      : snapshot.phase == RunPhase::cancelled
          ? ImVec4{1.0F, 0.75F, 0.25F, 1.0F}
          : ImVec4{1.0F, 0.35F, 0.3F, 1.0F};
  const std::string heading = runResultHeading(snapshot);
  ImGui::TextColored(status_color, "%s", heading.c_str());
  if (snapshot.phase == RunPhase::complete
      && !snapshot.run_directory.empty()) {
    ImGui::SetItemTooltip("%s", snapshot.run_directory.c_str());
  }

  if (snapshot.outcome
      && ImGui::BeginTable("Result values", 2, ImGuiTableFlags_SizingStretchProp)) {
    const auto row = [](const char* label) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextDisabled("%s", label);
      ImGui::TableSetColumnIndex(1);
    };
    row("Runtime");
    ImGui::Text("%.2f s", snapshot.outcome->runtime_seconds);
    row("Extracted keyframes");
    ImGui::Text("%zu", snapshot.outcome->selected_keyframes);
    row("Frames/keyframe");
    if (const auto mean = snapshot.outcome->meanFramesPerKeyframe()) {
      ImGui::Text("%.2f", *mean);
    } else {
      ImGui::TextDisabled("Unavailable");
    }
    ImGui::EndTable();
  }

  if (!snapshot.error.empty()) {
    ImGui::TextWrapped("%s", snapshot.error.c_str());
  } else if (snapshot.outcome && !snapshot.outcome->outputs_finalized) {
    ImGui::TextDisabled(
        snapshot.outcome->selected_keyframes > 0U
            ? "Partial keyframe files may be available."
            : "No keyframes were written.");
  }

  RunResultAction action = RunResultAction::none;
  constexpr float sidebar_button_width = 130.0F;
  ImGui::BeginDisabled(snapshot.run_directory.empty());
  if (ImGui::Button("Open run folder", ImVec2{sidebar_button_width, 0.0F})) {
    action = RunResultAction::open_run_folder;
  }
  ImGui::BeginDisabled(
      !snapshot.outcome || !snapshot.outcome->outputs_finalized);
  if (ImGui::Button("Open summary", ImVec2{sidebar_button_width, 0.0F})) {
    action = RunResultAction::open_summary;
  }
  ImGui::EndDisabled();
  ImGui::EndDisabled();
  return action;
}

std::optional<double> drawTimeline(
    const char* id,
    const TimelineView& view,
    ImVec2 size) {
  ImGui::PushID(id);
  const ImVec2 position = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton(
      "timeline",
      size,
      ImGuiButtonFlags_MouseButtonLeft);
  const ImVec2 end{position.x + size.x, position.y + size.y};
  const auto timeAtMouse = [&] {
    const float fraction = std::clamp(
        (ImGui::GetIO().MousePos.x - position.x) / std::max(size.x, 1.0F),
        0.0F,
        1.0F);
    return static_cast<double>(fraction) * view.duration_seconds;
  };
  const bool seeking = view.interactive && view.duration_seconds > 0.0
      && ImGui::IsItemActive();
  const std::optional<double> interaction_time = seeking
      ? std::optional<double>{timeAtMouse()}
      : std::nullopt;
  const double visual_playhead_seconds = interaction_time.value_or(
      view.playhead_seconds);
  std::optional<double> requested_time;
  // Holding the pointer still must not restart the asynchronous seek each frame.
  if (interaction_time
      && (ImGui::IsItemActivated() || ImGui::GetIO().MouseDelta.x != 0.0F)) {
    requested_time = interaction_time;
  }

  auto* const draw_list = ImGui::GetWindowDrawList();
  draw_list->AddRectFilled(position, end, IM_COL32(42, 45, 51, 255), 4.0F);

  const double safe_duration = std::max(view.duration_seconds, 1.0e-9);
  const auto toX = [&](double seconds) {
    const float fraction = static_cast<float>(
        std::clamp(seconds / safe_duration, 0.0, 1.0));
    return position.x + fraction * size.x;
  };
  const auto drawSelected = [&](double start, double finish, bool active) {
    const ImVec2 selected_start{toX(start), position.y};
    const ImVec2 selected_end{toX(finish), end.y};
    draw_list->AddRectFilled(
        selected_start,
        selected_end,
        active ? IM_COL32(55, 155, 210, 255) : IM_COL32(45, 125, 142, 255));
    if (view.progress_seconds) {
      const double completed_until = std::clamp(*view.progress_seconds, start, finish);
      if (completed_until > start) {
        draw_list->AddRectFilled(
            selected_start,
            ImVec2{toX(completed_until), selected_end.y},
            IM_COL32(43, 178, 118, 255));
      }
    }
  };
  if (view.regions.empty()) {
    drawSelected(0.0, safe_duration, false);
  } else {
    constexpr double tolerance = 1.0e-9;
    for (const auto& region : view.regions) {
      const bool active =
          visual_playhead_seconds >= region.start_seconds - tolerance
          && visual_playhead_seconds <= region.end_seconds + tolerance;
      drawSelected(
          region.start_seconds,
          region.end_seconds,
          active);
      draw_list->AddLine(
          ImVec2{toX(region.start_seconds), position.y},
          ImVec2{toX(region.start_seconds), end.y},
          IM_COL32(104, 219, 219, 255));
      draw_list->AddLine(
          ImVec2{toX(region.end_seconds), position.y},
          ImVec2{toX(region.end_seconds), end.y},
          IM_COL32(104, 219, 219, 255));
    }
  }
  if (view.pending_in_seconds) {
    const float x = toX(*view.pending_in_seconds);
    draw_list->AddLine(
        ImVec2{x, position.y - 2.0F},
        ImVec2{x, end.y + 2.0F},
        IM_COL32(255, 190, 70, 255),
        2.0F);
  }
  draw_list->AddRect(position, end, IM_COL32(90, 95, 105, 255), 4.0F);

  const float playhead_x = std::round(toX(visual_playhead_seconds));
  const ImU32 playhead_color = view.pending_in_seconds
      ? IM_COL32(55, 155, 210, 255)
      : IM_COL32(255, 70, 70, 255);
  draw_list->AddRectFilled(
      ImVec2{playhead_x - 1.0F, position.y + 3.0F},
      ImVec2{playhead_x + 1.0F, end.y + 3.0F},
      playhead_color);
  draw_list->AddTriangleFilled(
      ImVec2{playhead_x - 5.0F, position.y - 3.0F},
      ImVec2{playhead_x + 5.0F, position.y - 3.0F},
      ImVec2{playhead_x, position.y + 4.0F},
      playhead_color);

  ImGui::PopID();
  return requested_time;
}

void drawMetricPlot(
    const char* id,
    const char* title,
    const std::vector<MetricSample>& history,
    double MetricSample::* value_member,
    double threshold,
    const std::array<std::uint8_t, 3>& value_color,
    const VisualizationConfig& visualization,
    ImVec2 size,
    std::optional<double> fixed_max) {
  ImGui::PushID(id);
  const ImVec2 position = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("plot", size);
  auto* const draw_list = ImGui::GetWindowDrawList();
  const ImVec2 end{position.x + size.x, position.y + size.y};
  draw_list->AddRectFilled(position, end, IM_COL32(18, 18, 18, 255), 4.0F);
  draw_list->AddRect(position, end, IM_COL32(70, 70, 70, 255), 4.0F);

  constexpr float padding = 10.0F;
  const float title_height = ImGui::GetTextLineHeight() + 5.0F;
  const ImVec2 plot_min{position.x + padding, position.y + title_height + 6.0F};
  const ImVec2 plot_max{end.x - padding, end.y - padding};
  draw_list->AddText(
      ImVec2{position.x + padding, position.y + 5.0F},
      ImGui::GetColorU32(ImGuiCol_Text),
      title);
  if (history.empty() || plot_max.x <= plot_min.x || plot_max.y <= plot_min.y) {
    ImGui::PopID();
    return;
  }

  const double x_min = static_cast<double>(history.front().frame_index);
  const double x_max = std::max(
      x_min + 1.0, static_cast<double>(history.back().frame_index));
  double y_max = fixed_max.value_or(1.0);
  if (!fixed_max) {
    y_max = std::max(threshold, 1.0);
    for (const auto& sample : history) {
      y_max = std::max(y_max, sample.*value_member);
    }
    y_max *= 1.15;
  }

  const auto toPosition = [&](const MetricSample& sample, double value) {
    const float x_fraction = static_cast<float>(
        (static_cast<double>(sample.frame_index) - x_min) / (x_max - x_min));
    const float y_fraction = static_cast<float>(
        std::clamp(value / std::max(y_max, 1.0e-6), 0.0, 1.0));
    return ImVec2{
        plot_min.x + x_fraction * (plot_max.x - plot_min.x),
        plot_max.y - y_fraction * (plot_max.y - plot_min.y)};
  };

  draw_list->PushClipRect(plot_min, plot_max, true);
  const ImDrawListFlags previous_draw_flags = draw_list->Flags;
  draw_list->Flags |= ImDrawListFlags_AntiAliasedLines;
  draw_list->Flags &= ~ImDrawListFlags_AntiAliasedLinesUseTex;
  const float threshold_y = toPosition(history.front(), threshold).y;
  drawDashedHorizontalLine(
      draw_list,
      plot_min.x,
      plot_max.x,
      threshold_y,
      colorFromRgb(visualization.threshold_line_color_rgb));
  for (const auto& sample : history) {
    if (sample.triggered) {
      const float x = toPosition(sample, 0.0).x;
      draw_list->AddLine(
          ImVec2{x, plot_min.y},
          ImVec2{x, plot_max.y},
          colorFromRgb(visualization.trigger_line_color_rgb));
    }
  }

  std::vector<ImVec2> points;
  points.reserve(history.size());
  for (const auto& sample : history) {
    points.push_back(toPosition(sample, sample.*value_member));
  }
  if (points.size() > 1U) {
    draw_list->AddPolyline(
        points.data(),
        static_cast<int>(points.size()),
        colorFromRgb(value_color),
        ImDrawFlags_None,
        2.0F);
  } else {
    draw_list->AddCircleFilled(points.front(), 2.5F, colorFromRgb(value_color));
  }
  draw_list->Flags = previous_draw_flags;
  draw_list->PopClipRect();

  const double latest = history.back().*value_member;
  char latest_label[64];
  std::snprintf(
      latest_label,
      sizeof(latest_label),
      "%.2f %s %.2f",
      latest,
      comparisonSymbol(latest, threshold),
      threshold);
  const ImVec2 label_size = ImGui::CalcTextSize(latest_label);
  draw_list->AddText(
      ImVec2{end.x - padding - label_size.x, position.y + 5.0F},
      ImGui::GetColorU32(ImGuiCol_TextDisabled),
      latest_label);

  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Frame %lld\n%s: %.3f\nThreshold: %.3f",
        static_cast<long long>(history.back().frame_index),
        title,
        latest,
        threshold);
  }
  ImGui::PopID();
}

}  // namespace frame_extractor::gui
