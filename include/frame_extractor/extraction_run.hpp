#pragma once

#include "frame_extractor/config.hpp"
#include "frame_extractor/output.hpp"
#include "frame_extractor/processor.hpp"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace frame_extractor {

enum class ExtractionRunStatus {
  completed,
  cancelled,
  failed,
};

struct ExtractionRunRequest {
  std::filesystem::path input_video;
  std::optional<std::filesystem::path> output_directory;
  Config config;
  ProcessOptions process_options;
  RunOutputOptions output_options;
};

struct ExtractionRunResult {
  ExtractionRunStatus status{ExtractionRunStatus::failed};
  ProcessingResult processing;
  std::optional<VideoInfo> video_info;
  std::optional<RunPaths> output_paths;
  std::chrono::duration<double> runtime{};
  bool outputs_finalized{};
  std::string error;
};

using RunOutputStartedCallback = std::function<void(const RunPaths&)>;

// Owns decoder setup, processing, output finalization, and empty-run cleanup.
// Failures are returned so CLI and GUI runs have identical recovery behavior.
[[nodiscard]] ExtractionRunResult runExtraction(
    ExtractionRunRequest request,
    DiagnosticObserver* observer = nullptr,
    const CancellationToken* cancellation = nullptr,
    SelectedFrameSink* additional_selected_frame_sink = nullptr,
    FramePreviewSink* frame_preview_sink = nullptr,
    RunOutputStartedCallback output_started = {});

}  // namespace frame_extractor
