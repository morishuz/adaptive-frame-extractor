#pragma once

#include "model.hpp"
#include "preview_state.hpp"
#include "project_state.hpp"

#include <optional>
#include <string_view>

namespace frame_extractor::gui {

enum class ControlAction {
  none,
  choose_video,
  choose_output_directory,
  start_extraction,
  cancel_extraction,
  open_run_directory,
  open_summary,
};

struct ControlSidebarView {
  bool controls_enabled{};
  std::string_view input_video;
  std::string_view output_directory;
  std::string_view dialog_error;
  std::optional<double> source_fps;
  const RunSnapshot& snapshot;
  const PreviewState& preview;
};

struct ControlSidebarResult {
  ControlAction action{ControlAction::none};
  bool project_changed{};
};

[[nodiscard]] ControlSidebarResult drawControlSidebar(
    const ControlSidebarView& view,
    ProjectState& project,
    float height);

void drawLivePreview(const PreviewState& preview, float height);

}  // namespace frame_extractor::gui
