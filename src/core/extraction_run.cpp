#include "frame_extractor/extraction_run.hpp"

#include "frame_extractor/decoder.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace frame_extractor {
namespace {

using Clock = std::chrono::steady_clock;

class CombinedSelectedFrameSink final : public SelectedFrameSink {
 public:
  CombinedSelectedFrameSink(SelectedFrameSink& first, SelectedFrameSink& second)
      : first_{first}, second_{second} {}

  void onFrameSelected(const SelectedFrame& selected, const cv::Mat& frame_bgr) override {
    first_.onFrameSelected(selected, frame_bgr);
    second_.onFrameSelected(selected, frame_bgr);
  }

  void onSelectionUpdated(const SelectedFrame& selected) override {
    first_.onSelectionUpdated(selected);
    second_.onSelectionUpdated(selected);
  }

 private:
  SelectedFrameSink& first_;
  SelectedFrameSink& second_;
};

std::string currentExceptionMessage() {
  try {
    throw;
  } catch (const std::exception& error) {
    return error.what();
  } catch (...) {
    return "Unknown extraction failure";
  }
}

void appendError(std::string& destination, std::string message) {
  if (message.empty()) {
    return;
  }
  if (!destination.empty()) {
    destination += "; ";
  }
  destination += std::move(message);
}

}  // namespace

ExtractionRunResult runExtraction(
    ExtractionRunRequest request,
    DiagnosticObserver* observer,
    const CancellationToken* cancellation,
    SelectedFrameSink* additional_selected_frame_sink,
    FramePreviewSink* frame_preview_sink,
    RunOutputStartedCallback output_started) {
  ExtractionRunResult outcome;
  const auto started = Clock::now();
  std::unique_ptr<RunOutputWriter> writer;

  try {
    const bool adaptive_selection = !request.process_options.fixed_frame_interval;
    VideoDecoder decoder{
        request.input_video,
        VideoDecoderOptions{
            .target_analysis_area_px = adaptive_selection
                ? std::optional<int>{request.config.target_analysis_area_px}
                : std::nullopt,
            .defer_full_bgr = true}};
    outcome.video_info = decoder.info();

    if (request.output_directory) {
      writer = std::make_unique<RunOutputWriter>(
          *request.output_directory, request.config, request.output_options);
      outcome.output_paths = writer->paths();
      if (output_started) {
        output_started(*outcome.output_paths);
      }
    }

    std::unique_ptr<CombinedSelectedFrameSink> combined_sink;
    SelectedFrameSink* selected_sink = additional_selected_frame_sink;
    if (writer && additional_selected_frame_sink != nullptr) {
      combined_sink = std::make_unique<CombinedSelectedFrameSink>(
          *writer, *additional_selected_frame_sink);
      selected_sink = combined_sink.get();
    } else if (writer) {
      selected_sink = writer.get();
    }

    outcome.processing = processFrames(
        decoder,
        request.config,
        request.process_options,
        observer,
        cancellation,
        selected_sink,
        frame_preview_sink,
        &outcome.processing);
    outcome.runtime = std::chrono::duration<double>(Clock::now() - started);

    if (writer) {
      if (outcome.processing.processed_frames > 0U) {
        outcome.runtime = writer->finalize(
            request.input_video,
            request.process_options,
            *outcome.video_info,
            outcome.processing,
            outcome.runtime);
        outcome.outputs_finalized = true;
      } else {
        writer->discardEmpty();
        outcome.output_paths.reset();
      }
    }
    outcome.status = outcome.processing.cancelled
        ? ExtractionRunStatus::cancelled
        : ExtractionRunStatus::completed;
    return outcome;
  } catch (...) {
    outcome.error = currentExceptionMessage();
  }

  outcome.status = ExtractionRunStatus::failed;
  outcome.runtime = std::chrono::duration<double>(Clock::now() - started);
  if (!writer) {
    return outcome;
  }

  try {
    if (!outcome.processing.selected_frames.empty() && outcome.video_info) {
      outcome.runtime = writer->finalize(
          request.input_video,
          request.process_options,
          *outcome.video_info,
          outcome.processing,
          outcome.runtime,
          outcome.error);
      outcome.outputs_finalized = true;
    } else {
      writer->discardEmpty();
      outcome.output_paths.reset();
    }
  } catch (...) {
    appendError(
        outcome.error,
        "Could not finalize partial output: " + currentExceptionMessage());
  }
  return outcome;
}

}  // namespace frame_extractor
