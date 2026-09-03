#include "app_panels.hpp"

#include "platform.hpp"
#include "widgets.hpp"

#include "imgui.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

namespace frame_extractor::gui {
namespace {

constexpr float sidebar_button_width = 130.0F;
constexpr float tracking_point_radius = 1.5F;

void drawCompactPathLabel(std::string_view path, const char* empty_label) {
  if (path.empty()) {
    ImGui::TextDisabled("%s", empty_label);
    return;
  }

  const std::filesystem::path filesystem_path = pathFromUtf8(path);
  auto display = pathToUtf8(filesystem_path.filename());
  if (display.empty()) {
    display = pathToUtf8(filesystem_path.parent_path().filename());
  }
  if (display.empty()) {
    display = path;
  }
  ImGui::TextDisabled("%s", display.c_str());
  ImGui::SetItemTooltip("%.*s", static_cast<int>(path.size()), path.data());
}

void drawTrackingDiagnostics(
    const PreviewState& preview,
    std::size_t selected_keyframes) {
  const auto& style = ImGui::GetStyle();
  ImGui::PushStyleVar(
      ImGuiStyleVar_ItemSpacing, ImVec2{style.ItemSpacing.x, 2.0F});
  ImGui::PushStyleVar(
      ImGuiStyleVar_CellPadding, ImVec2{style.CellPadding.x, 2.0F});
  ImGui::SeparatorText("Tracking diagnostics");
  constexpr ImGuiTableFlags table_flags = ImGuiTableFlags_BordersInnerV
      | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX;
  if (ImGui::BeginTable("Diagnostic values", 2, table_flags)) {
    ImGui::TableSetupColumn("Metric", ImGuiTableColumnFlags_WidthFixed, 112.0F);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
    const auto row = [](const char* label) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextDisabled("%s", label);
      ImGui::TableSetColumnIndex(1);
    };
    row("Frame");
    ImGui::Text("%lld", static_cast<long long>(preview.frame_index));
    row("Time");
    ImGui::Text("%.3f s", preview.timestamp_seconds);
    row("Since trigger");
    ImGui::Text("%d frames", preview.frames_since_keyframe);
    row("Keyframes");
    ImGui::Text("%zu", selected_keyframes);
    ImGui::EndTable();
  }
  ImGui::PopStyleVar(2);
}

}  // namespace

ControlSidebarResult drawControlSidebar(
    const ControlSidebarView& view,
    ProjectState& project,
    float height) {
  ControlSidebarResult result;
  const auto setAction = [&](ControlAction action) {
    if (result.action == ControlAction::none) {
      result.action = action;
    }
  };

  ImGui::BeginChild(
      "Control sidebar",
      ImVec2{0.0F, height},
      ImGuiChildFlags_Borders);
  ImGui::SeparatorText("Input and output");
  ImGui::TextUnformatted("Input video");
  ImGui::BeginDisabled(!view.controls_enabled);
  if (ImGui::Button("Choose video...", ImVec2{sidebar_button_width, 0.0F})) {
    setAction(ControlAction::choose_video);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  drawCompactPathLabel(view.input_video, "Not selected");

  ImGui::TextUnformatted("Output folder");
  ImGui::BeginDisabled(!view.controls_enabled);
  if (ImGui::Button("Choose folder...", ImVec2{sidebar_button_width, 0.0F})) {
    setAction(ControlAction::choose_output_directory);
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  drawCompactPathLabel(view.output_directory, "Not selected");

  ImGui::SeparatorText("Output options");
  ImGui::TextUnformatted("Image format");
  ImGui::BeginDisabled(!view.controls_enabled);
  if (ImGui::RadioButton("JPEG", project.image_format == ImageFormat::jpg)) {
    project.image_format = ImageFormat::jpg;
    result.project_changed = true;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton(
          "PNG (lossless)", project.image_format == ImageFormat::png)) {
    project.image_format = ImageFormat::png;
    result.project_changed = true;
  }
  ImGui::EndDisabled();
  ImGui::Spacing();
  ImGui::BeginDisabled(!view.controls_enabled || project.regions.empty());
  if (ImGui::Checkbox(
          "Separate region folders",
          &project.group_keyframes_by_region)) {
    result.project_changed = true;
  }
  ImGui::EndDisabled();
  if (project.regions.empty()) {
    ImGui::SetItemTooltip("Define one or more regions to enable region folders.");
  }

  ImGui::SeparatorText("Extraction");
  ImGui::TextUnformatted("Selection");
  ImGui::BeginDisabled(!view.controls_enabled);
  const auto profileRadio = [&](const char* label, SelectionChoice choice) {
    if (ImGui::RadioButton(label, project.selection == choice)) {
      project.selection = choice;
      result.project_changed = true;
    }
  };
  profileRadio("Low", SelectionChoice::low);
  ImGui::SameLine();
  profileRadio("Medium", SelectionChoice::medium);
  ImGui::SameLine();
  profileRadio("High", SelectionChoice::high);
  ImGui::SameLine();
  profileRadio("Fixed interval", SelectionChoice::fixed);
  if (project.selection == SelectionChoice::fixed) {
    ImGui::TextUnformatted("Frame ratio");
    for (const int interval : supported_frame_intervals) {
      const std::string label = "1/" + std::to_string(interval);
      if (ImGui::RadioButton(label.c_str(), project.frame_interval == interval)) {
        project.frame_interval = interval;
        result.project_changed = true;
      }
      if (interval != supported_frame_intervals.back()) {
        ImGui::SameLine();
      }
    }
    if (view.source_fps) {
      ImGui::TextDisabled(
          "Approximately %.2f fps",
          *view.source_fps / static_cast<double>(project.frame_interval));
    }
  }
  ImGui::EndDisabled();

  const bool can_start = view.controls_enabled
      && !view.input_video.empty() && !view.output_directory.empty();
  ImGui::SeparatorText("Actions");
  ImGui::BeginDisabled(!can_start);
  const char* const start_label = isTerminal(view.snapshot.phase)
      ? "Run again"
      : "Start extraction";
  if (ImGui::Button(start_label, ImVec2{sidebar_button_width, 0.0F})) {
    setAction(ControlAction::start_extraction);
  }
  ImGui::EndDisabled();
  ImGui::BeginDisabled(
      !isActive(view.snapshot.phase)
      || view.snapshot.phase == RunPhase::cancelling);
  if (ImGui::Button("Cancel (Esc)", ImVec2{sidebar_button_width, 0.0F})) {
    setAction(ControlAction::cancel_extraction);
  }
  ImGui::EndDisabled();
  if (!isTerminal(view.snapshot.phase)) {
    ImGui::TextWrapped(
        "%s",
        view.snapshot.phase == RunPhase::idle && can_start
            ? "Ready."
            : view.snapshot.status.c_str());
  }
  if (!view.dialog_error.empty()) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0F, 0.45F, 0.35F, 1.0F});
    ImGui::TextWrapped(
        "%.*s",
        static_cast<int>(view.dialog_error.size()),
        view.dialog_error.data());
    ImGui::PopStyleColor();
  }
  if (isTerminal(view.snapshot.phase)) {
    switch (drawRunResultPanel(view.snapshot)) {
      case RunResultAction::open_run_folder:
        setAction(ControlAction::open_run_directory);
        break;
      case RunResultAction::open_summary:
        setAction(ControlAction::open_summary);
        break;
      case RunResultAction::none:
        break;
    }
  } else {
    drawTrackingDiagnostics(view.preview, view.snapshot.selected_keyframes);
  }
  ImGui::EndChild();
  return result;
}

void drawLivePreview(const PreviewState& preview, float height) {
  ImGui::SeparatorText("Live preview");
  ImGui::BeginChild(
      "Preview",
      ImVec2{0.0F, height},
      ImGuiChildFlags_Borders);
  if (!preview.texture.valid()) {
    ImGui::TextDisabled("The currently processed video frame will appear here.");
    ImGui::EndChild();
    return;
  }

  const ImVec2 image_available = ImGui::GetContentRegionAvail();
  const ImVec2 image_size = fitSize(
      preview.texture.width(), preview.texture.height(), image_available);
  const ImVec2 cursor = ImGui::GetCursorPos();
  ImGui::SetCursorPos(ImVec2{
      cursor.x + std::max(0.0F, (image_available.x - image_size.x) * 0.5F),
      cursor.y + std::max(0.0F, (image_available.y - image_size.y) * 0.5F)});
  const ImVec2 image_position = ImGui::GetCursorScreenPos();
  ImGui::Image(preview.texture.reference(), image_size);

  auto* const draw_list = ImGui::GetWindowDrawList();
  const ImVec2 image_end{
      image_position.x + image_size.x, image_position.y + image_size.y};
  draw_list->PushClipRect(image_position, image_end, true);
  for (const auto& point : preview.tracking_points) {
    const ImVec2 position{
        image_position.x + std::clamp(point.x, 0.0F, 1.0F) * image_size.x,
        image_position.y + std::clamp(point.y, 0.0F, 1.0F) * image_size.y};
    draw_list->AddCircleFilled(
        position, tracking_point_radius, IM_COL32(0, 0, 0, 255), 12);
  }
  draw_list->PopClipRect();
  ImGui::EndChild();
}

}  // namespace frame_extractor::gui
