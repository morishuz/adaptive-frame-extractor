#include "frame_extractor/build_info.hpp"
#include "frame_extractor/config.hpp"

#include "app_panels.hpp"
#include "app_preferences.hpp"
#include "extraction_controller.hpp"
#include "keyframe_strip.hpp"
#include "manual_frame_exporter.hpp"
#include "platform.hpp"
#include "preview_state.hpp"
#include "profile_configs.hpp"
#include "project_state.hpp"
#include "smoke_test.hpp"
#include "texture.hpp"
#include "video_scrubber.hpp"
#include "widgets.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_main.h>
#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fe = frame_extractor;
namespace gui = frame_extractor::gui;
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto keyframe_highlight_duration = std::chrono::milliseconds{100};

class DialogMailbox {
 public:
  void setSelection(const char* const* file_list) {
    std::lock_guard lock{mutex_};
    if (file_list == nullptr) {
      error_ = SDL_GetError();
    } else if (*file_list != nullptr) {
      selection_ = *file_list;
    }
  }

  std::optional<std::string> takeSelection() {
    std::lock_guard lock{mutex_};
    auto result = std::move(selection_);
    selection_.reset();
    return result;
  }

  std::string takeError() {
    std::lock_guard lock{mutex_};
    return std::exchange(error_, {});
  }

 private:
  std::mutex mutex_;
  std::optional<std::string> selection_;
  std::string error_;
};

void SDLCALL dialogCallback(void* userdata, const char* const* file_list, int) {
  static_cast<DialogMailbox*>(userdata)->setSelection(file_list);
}

std::string formatTime(double seconds) {
  const auto total_milliseconds = static_cast<long long>(
      std::llround(std::max(0.0, seconds) * 1000.0));
  const auto hours = total_milliseconds / 3'600'000;
  const auto minutes = (total_milliseconds / 60'000) % 60;
  const auto whole_seconds = (total_milliseconds / 1000) % 60;
  const auto milliseconds = total_milliseconds % 1000;
  char buffer[32];
  SDL_snprintf(
      buffer,
      sizeof(buffer),
      "%02lld:%02lld:%02lld.%03lld",
      hours,
      minutes,
      whole_seconds,
      milliseconds);
  return buffer;
}

void shutdownGui(SDL_Window* window, SDL_Renderer* renderer) {
  ImGui_ImplSDLRenderer3_Shutdown();
  ImGui_ImplSDL3_Shutdown();
  ImGui::DestroyContext();
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
}

int runApplication(int argc, char** argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--version") {
    std::cout << "frame-extractor-gui " << fe::build::display << '\n';
    return 0;
  }
  const bool smoke_test = argc >= 2 && std::string_view{argv[1]} == "--smoke-test";
  if (smoke_test && argc != 3) {
    std::cerr << "Usage: frame-extractor-gui --smoke-test INPUT_VIDEO\n";
    return 2;
  }
  if (argc > 3) {
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR,
        "Frame Extractor",
        "Usage: frame-extractor-gui [INPUT_VIDEO [OUTPUT_DIRECTORY]]",
        nullptr);
    return 2;
  }

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
    if (!smoke_test) {
      SDL_ShowSimpleMessageBox(
          SDL_MESSAGEBOX_ERROR, "Frame Extractor", SDL_GetError(), nullptr);
    }
    return 1;
  }

  const auto resource_directory = gui::applicationResourceDirectory();
  std::optional<gui::ProfileConfigs> profile_configs;
  try {
    profile_configs.emplace(
        gui::ProfileConfigs::load(resource_directory / "configs"));
  } catch (const std::exception& error) {
    std::cerr << "Profile configuration could not be loaded: " << error.what() << '\n';
    if (!smoke_test) {
      SDL_ShowSimpleMessageBox(
          SDL_MESSAGEBOX_ERROR, "Frame Extractor", error.what(), nullptr);
    }
    SDL_Quit();
    return 1;
  }

  const float display_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
  const std::string window_title =
      "Frame Extractor - " + std::string{fe::build::display};
  SDL_Window* window = SDL_CreateWindow(
      window_title.c_str(),
      static_cast<int>(1280.0F * display_scale),
      static_cast<int>(920.0F * display_scale),
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
  if (window == nullptr) {
    std::cerr << "Window creation failed: " << SDL_GetError() << '\n';
    SDL_Quit();
    return 1;
  }
  SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
  if (renderer == nullptr) {
    std::cerr << "Renderer creation failed: " << SDL_GetError() << '\n';
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
  SDL_SetRenderVSync(renderer, 1);
#if !defined(__APPLE__)
  const auto icon_path = resource_directory / "icons/FrameExtractor.png";
  const cv::Mat icon = cv::imread(icon_path.string(), cv::IMREAD_COLOR);
  SDL_Surface* icon_surface = icon.empty() ? nullptr : SDL_CreateSurfaceFrom(
      icon.cols, icon.rows, SDL_PIXELFORMAT_BGR24, icon.data, static_cast<int>(icon.step));
  const bool icon_loaded = icon_surface != nullptr && SDL_SetWindowIcon(window, icon_surface);
  SDL_DestroySurface(icon_surface);
  if (!icon_loaded && smoke_test) {
    std::cerr << "Application icon could not be loaded: " << icon_path << '\n';
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 1;
  }
#endif
  SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  SDL_ShowWindow(window);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  auto& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  auto& style = ImGui::GetStyle();
  style.FontSizeBase = 16.0F;
  style.ScaleAllSizes(display_scale);
  style.FontScaleDpi = display_scale;
  const auto font_path = resource_directory / "fonts/InterVariable.ttf";
  const bool font_loaded = std::filesystem::is_regular_file(font_path)
      && io.Fonts->AddFontFromFileTTF(font_path.string().c_str()) != nullptr;
  if (!font_loaded) {
    io.Fonts->AddFontDefaultVector();
  }
  ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
  ImGui_ImplSDLRenderer3_Init(renderer);

  if (smoke_test) {
    if (!font_loaded) {
      std::cerr << "Bundled Inter font could not be loaded: " << font_path << '\n';
    }
    const int result = font_loaded ? gui::runSmokeTest(renderer, argv[2]) : 1;
    shutdownGui(window, renderer);
    return result;
  }

  gui::ExtractionController controller;
  gui::VideoScrubber scrubber;
  gui::ManualFrameExporter manual_frame_exporter;
  gui::ProjectStore project_store;
  gui::AppPreferencesStore preferences_store;
  DialogMailbox video_dialog;
  DialogMailbox output_dialog;
  std::string dialog_error;
  gui::AppPreferences preferences;
  try {
    preferences = preferences_store.load();
  } catch (const std::exception& error) {
    dialog_error = error.what();
  }
  const auto discardMissingDirectory = [](std::filesystem::path& path) {
    std::error_code error;
    if (!path.empty() && !std::filesystem::is_directory(path, error)) {
      path.clear();
    }
  };
  discardMissingDirectory(preferences.last_input_directory);
  discardMissingDirectory(preferences.output_directory);
  std::string input_video = argc >= 2 ? argv[1] : "";
  std::string output_directory = argc >= 3
      ? argv[2]
      : gui::pathToUtf8(preferences.output_directory);
  if (!input_video.empty() && output_directory.empty()) {
    output_directory = gui::pathToUtf8(
        gui::pathFromUtf8(input_video).parent_path() / "frame-extractor-output");
  }
  gui::ProjectState project;
  std::optional<fe::VideoInfo> scrubber_video_info;
  std::optional<double> pending_in_seconds;
  double displayed_playhead_seconds{};
  std::optional<double> extraction_progress_seconds;
  bool project_dirty = false;
  Clock::time_point project_save_due{};
  gui::PreviewState preview;
  std::vector<gui::MetricSample> metric_history;
  gui::KeyframeStrip keyframe_strip;
  std::optional<std::size_t> highlighted_keyframe;
  Clock::time_point keyframe_highlight_until{};
  bool scroll_keyframes_to_latest = false;
  fe::Config gui_config = gui::configForProject(
      project, profile_configs->forSelection(project.selection));
  gui::RunSnapshot snapshot;
  gui::ManualFrameExportSnapshot manual_export_snapshot;
  Clock::time_point manual_export_notice_until{};
  bool done = false;
  bool close_requested = false;
  bool quit_after_cancel = false;

  static constexpr SDL_DialogFileFilter video_filters[] = {
      {"Video files", "mov;mp4;m4v;avi;mkv;webm"},
      {"All files", "*"},
  };

  const auto markProjectDirty = [&] {
    project_dirty = true;
    project_save_due = Clock::now() + std::chrono::milliseconds{500};
  };
  const auto savePreferences = [&] {
    try {
      preferences_store.save(preferences);
    } catch (const std::exception& error) {
      dialog_error = error.what();
    }
  };
  if (!output_directory.empty()) {
    preferences.output_directory = gui::pathFromUtf8(output_directory);
    savePreferences();
  }
  const auto invalidateCompletedRun = [&] {
    controller.reset();
    snapshot = controller.takeSnapshot();
    extraction_progress_seconds.reset();
  };
  const auto saveProject = [&] {
    if (input_video.empty() || !project_dirty) {
      return;
    }
    try {
      project_store.save(gui::pathFromUtf8(input_video), project);
      project_dirty = false;
    } catch (const std::exception& error) {
      dialog_error = error.what();
    }
  };
  const auto openInputVideo = [&](std::string selected) {
    dialog_error.clear();
    input_video = std::move(selected);
    const auto input_directory = gui::pathFromUtf8(input_video).parent_path();
    if (!input_directory.empty()) {
      preferences.last_input_directory = input_directory;
    }
    if (output_directory.empty()) {
      output_directory = gui::pathToUtf8(
          gui::pathFromUtf8(input_video).parent_path()
          / "frame-extractor-output");
      preferences.output_directory = gui::pathFromUtf8(output_directory);
    }
    savePreferences();
    preview.clear();
    metric_history.clear();
    keyframe_strip.clear();
    highlighted_keyframe.reset();
    scroll_keyframes_to_latest = false;
    scrubber_video_info.reset();
    pending_in_seconds.reset();
    extraction_progress_seconds.reset();
    controller.reset();
    manual_frame_exporter.reset();
    manual_export_snapshot = manual_frame_exporter.takeSnapshot();
    manual_export_notice_until = {};
    snapshot = controller.takeSnapshot();
    const auto loaded_project = project_store.loadOrDefault(
        gui::pathFromUtf8(input_video));
    project = loaded_project.state;
    gui_config = gui::configForProject(
        project, profile_configs->forSelection(project.selection));
    displayed_playhead_seconds = project.playhead_seconds;
    project_dirty = false;
    if (!loaded_project.warning.empty()) {
      dialog_error = loaded_project.warning;
    }
    try {
      scrubber.open(gui::pathFromUtf8(input_video), project.playhead_seconds);
    } catch (const std::exception& error) {
      dialog_error = error.what();
    }
  };
  const auto finishRegion = [&] {
    if (!pending_in_seconds) {
      return;
    }
    gui::addRegion(project, *pending_in_seconds, displayed_playhead_seconds);
    pending_in_seconds.reset();
    invalidateCompletedRun();
    markProjectDirty();
  };
  const auto removeRegionAtPlayhead = [&] {
    if (gui::removeRegionAt(project, displayed_playhead_seconds)) {
      invalidateCompletedRun();
      markProjectDirty();
    }
  };

  if (!input_video.empty()) {
    openInputVideo(input_video);
  }

  while (!done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL3_ProcessEvent(&event);
      if (event.type == SDL_EVENT_QUIT
          || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED
              && event.window.windowID == SDL_GetWindowID(window))) {
        close_requested = true;
      }
      if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE
          && !event.key.repeat && gui::isActive(snapshot.phase)) {
        controller.cancel();
      }
      if (event.type == SDL_EVENT_DROP_FILE && event.drop.data != nullptr
          && !gui::isActive(snapshot.phase)
          && manual_export_snapshot.phase != gui::ManualFrameExportPhase::saving) {
        saveProject();
        openInputVideo(event.drop.data);
      }
      if (event.type == SDL_EVENT_KEY_DOWN && !gui::isActive(snapshot.phase)
          && !input_video.empty()) {
        if (event.key.key == SDLK_LEFT) {
          scrubber.stepBackward();
        } else if (event.key.key == SDLK_RIGHT) {
          scrubber.stepForward();
        } else if (!event.key.repeat && event.key.key == SDLK_I) {
          pending_in_seconds = displayed_playhead_seconds;
        } else if (!event.key.repeat && event.key.key == SDLK_O) {
          finishRegion();
        } else if (!event.key.repeat
                   && (event.key.key == SDLK_DELETE || event.key.key == SDLK_BACKSPACE)) {
          removeRegionAtPlayhead();
        }
      }
    }

    if (const auto selected = video_dialog.takeSelection()) {
      saveProject();
      openInputVideo(*selected);
      if (output_directory.empty()) {
        output_directory = gui::pathToUtf8(
            gui::pathFromUtf8(input_video).parent_path()
            / "frame-extractor-output");
      }
    }
    if (const auto selected = output_dialog.takeSelection()) {
      output_directory = *selected;
      preferences.output_directory = gui::pathFromUtf8(output_directory);
      savePreferences();
    }
    const auto video_dialog_error = video_dialog.takeError();
    const auto output_dialog_error = output_dialog.takeError();
    if (!video_dialog_error.empty()) {
      dialog_error = video_dialog_error;
    } else if (!output_dialog_error.empty()) {
      dialog_error = output_dialog_error;
    }

    if (project_dirty && Clock::now() >= project_save_due) {
      saveProject();
    }

    auto scrubber_snapshot = scrubber.takeSnapshot();
    if (scrubber_snapshot.video_info) {
      scrubber_video_info = scrubber_snapshot.video_info;
    }
    if (!scrubber_snapshot.error.empty()) {
      dialog_error = scrubber_snapshot.error;
    }
    if (scrubber_snapshot.frame && !gui::isActive(snapshot.phase)
        && preview.update(renderer, *scrubber_snapshot.frame)) {
      displayed_playhead_seconds = preview.timestamp_seconds;
      project.playhead_seconds = preview.timestamp_seconds;
      markProjectDirty();
    }

    auto next_manual_export_snapshot = manual_frame_exporter.takeSnapshot();
    if (next_manual_export_snapshot.phase == gui::ManualFrameExportPhase::complete
        && manual_export_snapshot.phase == gui::ManualFrameExportPhase::saving) {
      manual_export_notice_until = Clock::now() + std::chrono::seconds{3};
    } else if (
        next_manual_export_snapshot.phase == gui::ManualFrameExportPhase::failed
        && manual_export_snapshot.phase != gui::ManualFrameExportPhase::failed
        && !next_manual_export_snapshot.error.empty()) {
      dialog_error = next_manual_export_snapshot.error;
    }
    manual_export_snapshot = std::move(next_manual_export_snapshot);

    const bool run_was_active = gui::isActive(snapshot.phase);
    snapshot = controller.takeSnapshot();
    if (snapshot.progress_timestamp_seconds) {
      extraction_progress_seconds = snapshot.progress_timestamp_seconds;
    }
    if (snapshot.preview
        && preview.update(renderer, std::move(*snapshot.preview))) {
      displayed_playhead_seconds = preview.timestamp_seconds;
    }
    if (gui::isActive(snapshot.phase) && extraction_progress_seconds) {
      displayed_playhead_seconds = *extraction_progress_seconds;
    }
    if (run_was_active && !gui::isActive(snapshot.phase)) {
      project.playhead_seconds = displayed_playhead_seconds;
      scrubber.seek(displayed_playhead_seconds);
      markProjectDirty();
    }
    gui::appendMetricSamples(metric_history, snapshot.metric_samples);
    for (auto& pending : snapshot.thumbnails) {
      if (keyframe_strip.append(renderer, pending)) {
        highlighted_keyframe = pending.keyframe_index;
        keyframe_highlight_until = Clock::now() + keyframe_highlight_duration;
        scroll_keyframes_to_latest = true;
      } else {
        dialog_error = "Could not create a keyframe preview texture.";
      }
    }
    if (quit_after_cancel && !gui::isActive(snapshot.phase)) {
      done = true;
      continue;
    }
    if (close_requested && !gui::isActive(snapshot.phase)
        && manual_export_snapshot.phase != gui::ManualFrameExportPhase::saving) {
      done = true;
      continue;
    }

    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2{0.0F, 0.0F});
    ImGui::SetNextWindowSize(io.DisplaySize);
    constexpr ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration
        | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;
    ImGui::Begin("Frame Extractor", nullptr, window_flags);

    const bool controls_enabled = !gui::isActive(snapshot.phase);
    const bool manual_export_saving =
        manual_export_snapshot.phase == gui::ManualFrameExportPhase::saving;
    const float thumbnail_height = keyframe_strip.empty() ? 55.0F : 155.0F;
    constexpr float plot_height = 122.0F;
    const bool show_metric_plots = project.selection != gui::SelectionChoice::fixed;
    const float plots_height = show_metric_plots
        ? plot_height + style.ItemSpacing.y
        : 0.0F;
    constexpr float timeline_panel_height = 70.0F;
    const ImVec2 workspace_available = ImGui::GetContentRegionAvail();
    const float workspace_height = std::max(
        260.0F,
        workspace_available.y - thumbnail_height - plots_height - timeline_panel_height);
    const float sidebar_width = std::clamp(
        workspace_available.x * 0.29F, 340.0F, 390.0F);

    ImGui::BeginTable("Workspace layout", 2, ImGuiTableFlags_SizingStretchProp);
    ImGui::TableSetupColumn(
        "Controls", ImGuiTableColumnFlags_WidthFixed, sidebar_width);
    ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableNextColumn();
    const auto source_fps = scrubber_video_info
        ? scrubber_video_info->framesPerSecond()
        : std::nullopt;
    const auto sidebar_result = gui::drawControlSidebar(
        gui::ControlSidebarView{
            .controls_enabled = controls_enabled && !manual_export_saving,
            .input_video = input_video,
            .output_directory = output_directory,
            .dialog_error = dialog_error,
            .source_fps = source_fps,
            .snapshot = snapshot,
            .preview = preview},
        project,
        workspace_height);
    if (sidebar_result.project_changed) {
      gui_config = gui::configForProject(
          project, profile_configs->forSelection(project.selection));
      invalidateCompletedRun();
      markProjectDirty();
    }
    switch (sidebar_result.action) {
      case gui::ControlAction::choose_video: {
        const std::string default_location = input_video.empty()
            ? gui::pathToUtf8(preferences.last_input_directory)
            : input_video;
        SDL_ShowOpenFileDialog(
            dialogCallback,
            &video_dialog,
            window,
            video_filters,
            static_cast<int>(std::size(video_filters)),
            default_location.empty() ? nullptr : default_location.c_str(),
            false);
        break;
      }
      case gui::ControlAction::choose_output_directory:
        SDL_ShowOpenFolderDialog(
            dialogCallback,
            &output_dialog,
            window,
            output_directory.empty() ? nullptr : output_directory.c_str(),
            false);
        break;
      case gui::ControlAction::start_extraction: {
        preview.prepareForRun();
        metric_history.clear();
        keyframe_strip.clear();
        highlighted_keyframe.reset();
        scroll_keyframes_to_latest = false;
        dialog_error.clear();
        extraction_progress_seconds.reset();
        saveProject();
        const bool started = controller.start(
            gui::pathFromUtf8(input_video),
            gui::pathFromUtf8(output_directory),
            gui_config,
            gui::processOptionsForProject(project, input_video),
            gui::outputOptionsForProject(project));
        if (started) {
          snapshot = gui::RunSnapshot{
              .phase = gui::RunPhase::running,
              .status = "Opening video..."};
        }
        break;
      }
      case gui::ControlAction::cancel_extraction:
        controller.cancel();
        break;
      case gui::ControlAction::open_run_directory:
      case gui::ControlAction::open_summary: {
        std::filesystem::path path = gui::pathFromUtf8(snapshot.run_directory);
        if (sidebar_result.action == gui::ControlAction::open_summary) {
          path /= "summary.txt";
        }
        [[maybe_unused]] const bool opened = gui::openPath(path, dialog_error);
        break;
      }
      case gui::ControlAction::none:
        break;
    }

    ImGui::TableNextColumn();
    const float preview_height = std::max(
        80.0F, workspace_height - ImGui::GetFrameHeightWithSpacing());
    gui::drawLivePreview(preview, preview_height);
    ImGui::EndTable();

    const double duration_seconds = scrubber_video_info
        ? scrubber_video_info->estimatedDurationSeconds().value_or(0.0)
        : 0.0;
    const auto active_region_index = gui::regionIndexAt(
        project.regions, displayed_playhead_seconds);
    auto timeline_progress = extraction_progress_seconds;
    if (snapshot.phase == gui::RunPhase::complete) {
      timeline_progress = duration_seconds;
    }
    if (const auto requested = gui::drawTimeline(
            "Video regions",
            gui::TimelineView{
                .duration_seconds = duration_seconds,
                .playhead_seconds = displayed_playhead_seconds,
                .regions = project.regions,
                .pending_in_seconds = pending_in_seconds,
                .progress_seconds = timeline_progress,
                .interactive = controls_enabled && duration_seconds > 0.0},
            ImVec2{ImGui::GetContentRegionAvail().x, 26.0F})) {
      displayed_playhead_seconds = *requested;
      project.playhead_seconds = *requested;
      scrubber.seek(*requested);
      markProjectDirty();
    }

    ImGui::BeginDisabled(!controls_enabled || !scrubber_video_info);
    ImGui::PushItemFlag(ImGuiItemFlags_ButtonRepeat, true);
    if (ImGui::Button("< Frame")) {
      scrubber.stepBackward();
    }
    ImGui::SameLine();
    if (ImGui::Button("Frame >")) {
      scrubber.stepForward();
    }
    ImGui::PopItemFlag();
    ImGui::SameLine();
    const bool can_export_frame = !manual_export_saving
        && !scrubber_snapshot.loading && preview.frame_index >= 0
        && !input_video.empty() && !output_directory.empty();
    ImGui::BeginDisabled(!can_export_frame);
    if (ImGui::Button("Extract")) {
      dialog_error.clear();
      if (manual_frame_exporter.start(
              gui::pathFromUtf8(input_video),
              gui::pathFromUtf8(output_directory),
              preview.timestamp_seconds,
              project.image_format)) {
        manual_export_snapshot = gui::ManualFrameExportSnapshot{
            .phase = gui::ManualFrameExportPhase::saving};
        manual_export_notice_until = {};
      } else {
        manual_export_snapshot = manual_frame_exporter.takeSnapshot();
        if (!manual_export_snapshot.error.empty()) {
          dialog_error = manual_export_snapshot.error;
        }
      }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      if (manual_export_saving) {
        ImGui::SetTooltip("Saving the current frame at source resolution...");
      } else if (
          manual_export_snapshot.phase == gui::ManualFrameExportPhase::complete
          && !manual_export_snapshot.output_path.empty()) {
        ImGui::SetTooltip(
            "%s",
            gui::pathToUtf8(manual_export_snapshot.output_path).c_str());
      } else {
        const std::string image_format = fe::toString(project.image_format);
        ImGui::SetTooltip(
            "Save the current source-resolution frame as %s",
            image_format.c_str());
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Set In (I)")) {
      pending_in_seconds = displayed_playhead_seconds;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!pending_in_seconds.has_value());
    if (ImGui::Button("Set Out (O)")) {
      finishRegion();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!active_region_index);
    if (ImGui::Button("Remove region")) {
      removeRegionAtPlayhead();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(project.regions.empty());
    if (ImGui::Button("Clear regions")) {
      project.regions.clear();
      pending_in_seconds.reset();
      invalidateCompletedRun();
      markProjectDirty();
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::SameLine();
    const std::string current_time = formatTime(displayed_playhead_seconds);
    const std::string total_time = formatTime(duration_seconds);
    if (pending_in_seconds) {
      const std::string in_time = formatTime(*pending_in_seconds);
      ImGui::TextDisabled("In");
      ImGui::SameLine();
      ImGui::TextColored(
          ImVec4{1.0F, 0.75F, 0.25F, 1.0F}, "%s", in_time.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("Out");
      ImGui::SameLine();
      ImGui::TextColored(
          ImVec4{0.22F, 0.61F, 0.82F, 1.0F}, "%s", current_time.c_str());

      if (const auto length = gui::estimateRegionLength(
              *pending_in_seconds, displayed_playhead_seconds, source_fps)) {
        ImGui::SameLine();
        ImGui::TextDisabled("Length");
        ImGui::SameLine();
        const std::string length_time = formatTime(length->seconds);
        if (length->inclusive_frames) {
          ImGui::Text(
              "%s (~%lld frames)",
              length_time.c_str(),
              static_cast<long long>(*length->inclusive_frames));
        } else {
          ImGui::Text("%s", length_time.c_str());
        }
      }
    } else {
      ImGui::TextDisabled(
          "%s / %s", current_time.c_str(), total_time.c_str());
    }
    if (snapshot.phase != gui::RunPhase::idle && snapshot.processed_frames > 0U) {
      ImGui::SameLine();
      if (snapshot.total_frames) {
        ImGui::TextDisabled(
            "Processed %zu / %zu",
            snapshot.processed_frames,
            *snapshot.total_frames);
      } else {
        ImGui::TextDisabled("Processed %zu", snapshot.processed_frames);
      }
    }
    if (manual_export_snapshot.phase == gui::ManualFrameExportPhase::saving) {
      ImGui::SameLine();
      ImGui::TextDisabled("Saving frame...");
    } else if (
        manual_export_snapshot.phase == gui::ManualFrameExportPhase::complete
        && Clock::now() < manual_export_notice_until) {
      ImGui::SameLine();
      const auto filename = manual_export_snapshot.output_path.filename().string();
      ImGui::TextColored(
          ImVec4{0.30F, 0.82F, 0.52F, 1.0F},
          manual_export_snapshot.already_exists
              ? "Already saved: %s"
              : "Saved: %s",
          filename.c_str());
    }
    if (show_metric_plots) {
      const float plot_gap = style.ItemSpacing.x;
      const float plot_width = std::max(
          1.0F, (ImGui::GetContentRegionAvail().x - plot_gap) * 0.5F);
      gui::drawMetricPlot(
          "Motion history",
          "Motion",
          metric_history,
          &gui::MetricSample::motion_score,
          gui_config.trigger.main_threshold_analysis_px,
          gui_config.visualization.motion_plot_color_rgb,
          gui_config.visualization,
          ImVec2{plot_width, plot_height});
      ImGui::SameLine(0.0F, plot_gap);
      gui::drawMetricPlot(
          "Point history",
          "Points in bounds",
          metric_history,
          &gui::MetricSample::in_bounds_ratio,
          gui_config.trigger.min_in_bounds_ratio,
          gui_config.visualization.points_plot_color_rgb,
          gui_config.visualization,
          ImVec2{plot_width, plot_height},
          1.0);
    }

    ImGui::SeparatorText("Selected keyframes");
    keyframe_strip.draw(
        highlighted_keyframe,
        Clock::now() < keyframe_highlight_until,
        scroll_keyframes_to_latest);
    scroll_keyframes_to_latest = false;

    if (close_requested && gui::isActive(snapshot.phase)) {
      ImGui::OpenPopup("Extraction is running");
      close_requested = false;
    }
    if (ImGui::BeginPopupModal(
            "Extraction is running", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Cancel extraction and close the application?");
      ImGui::TextDisabled("Completed keyframes will remain on disk.");
      if (ImGui::Button("Cancel and quit", ImVec2{150.0F, 0.0F})) {
        controller.cancel();
        quit_after_cancel = true;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Keep running", ImVec2{130.0F, 0.0F})) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    ImGui::End();
    ImGui::Render();
    SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(renderer, 18, 21, 26, 255);
    SDL_RenderClear(renderer);
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
    SDL_RenderPresent(renderer);
  }

  controller.cancel();
  controller.join();
  saveProject();
  savePreferences();
  keyframe_strip.clear();
  preview.clear();
  shutdownGui(window, renderer);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  return runApplication(argc, argv);
}
