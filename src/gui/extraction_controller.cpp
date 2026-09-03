#include "extraction_controller.hpp"
#include "image_utils.hpp"
#include "platform.hpp"

#include "frame_extractor/diagnostics.hpp"
#include "frame_extractor/extraction_run.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <exception>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <variant>

namespace frame_extractor::gui {
namespace {

using Clock = std::chrono::steady_clock;
constexpr auto preview_interval = std::chrono::microseconds{66'667};

struct SharedRunState {
  std::mutex mutex;
  RunSnapshot snapshot{
      .phase = RunPhase::idle,
      .status = "Choose a video and output folder to begin."};
};

class GuiRunBridge final : public DiagnosticObserver,
                           public SelectedFrameSink,
                           public FramePreviewSink {
 public:
  GuiRunBridge(SharedRunState& state, bool collect_metrics)
      : state_{state}, collect_metrics_{collect_metrics} {}

  void onEvent(const DiagnosticEvent& event) override {
    std::lock_guard lock{state_.mutex};
    auto& snapshot = state_.snapshot;
    if (const auto* started = std::get_if<RunStartedEvent>(&event)) {
      snapshot.total_frames = started->total_frames;
      snapshot.status = "Processing video...";
    } else if (const auto* frame = std::get_if<FrameAnalyzedEvent>(&event)) {
      snapshot.processed_frames = frame->processed_index + 1U;
      snapshot.progress_timestamp_seconds = frame->scores.timestamp_sec;
      if (collect_metrics_) {
        snapshot.metric_samples.push_back(MetricSample{
            frame->scores.frame_index,
            frame->scores.global_score,
            frame->scores.in_bounds_ratio,
            frame->trigger.triggered,
            frame->region_index});
        trimToMostRecent(snapshot.metric_samples, metric_history_capacity);
      }
    } else if (const auto* selected = std::get_if<KeyframeSelectedEvent>(&event)) {
      snapshot.selected_keyframes = selected->keyframe_index + 1U;
    } else if (const auto* warning = std::get_if<WarningEvent>(&event)) {
      snapshot.status = "Warning: " + warning->message;
    } else if (const auto* finished = std::get_if<RunFinishedEvent>(&event)) {
      snapshot.processed_frames = finished->processed_frames;
      snapshot.selected_keyframes = finished->selected_keyframes;
      snapshot.status = "Finalizing output...";
    }
  }

  void onFrameSelected(const SelectedFrame& selected, const cv::Mat& frame_bgr) override {
    auto thumbnail = boundedCopy(frame_bgr, 240, 150);
    std::lock_guard lock{state_.mutex};
    state_.snapshot.thumbnails.push_back(PendingThumbnail{
        std::move(thumbnail),
        selected.keyframe_index,
        selected.decoded_frame_index});
    trimToMostRecent(
        state_.snapshot.thumbnails,
        thumbnail_history_capacity);
  }

  std::optional<cv::Size> requestedFrameSize(
      const FrameAnalyzedEvent& analyzed) override {
    const auto now = Clock::now();
    if (!analyzed.trigger.triggered && last_preview_ != Clock::time_point{}
        && now - last_preview_ < preview_interval) {
      return std::nullopt;
    }
    last_preview_ = now;
    return cv::Size{1280, 720};
  }

  void onFrameAnalyzed(
      const FrameAnalyzedEvent& analyzed,
      const cv::Mat& frame_bgr,
      const TrackingState& tracking,
      const FlowStepDiagnostics& diagnostics,
      int tracking_frame_width,
      int tracking_frame_height) override {
    std::vector<Point2f> normalized_points;
    if (tracking_frame_width > 0 && tracking_frame_height > 0) {
      const auto point_count = std::min({
          tracking.current_points.size(),
          tracking.alive_mask.size(),
          diagnostics.in_bounds_mask.size()});
      normalized_points.reserve(point_count);
      for (std::size_t index = 0; index < point_count; ++index) {
        if (tracking.alive_mask[index] == 0U || diagnostics.in_bounds_mask[index] == 0U) {
          continue;
        }
        normalized_points.push_back(Point2f{
            tracking.current_points[index].x / static_cast<float>(tracking_frame_width),
            tracking.current_points[index].y / static_cast<float>(tracking_frame_height)});
      }
    }

    auto preview = frame_bgr.clone();
    std::lock_guard lock{state_.mutex};
    state_.snapshot.preview = PendingImage{
        std::move(preview),
        analyzed.scores.frame_index,
        analyzed.scores.timestamp_sec,
        analyzed.trigger.frames_since_keyframe,
        std::move(normalized_points)};
  }

 private:
  SharedRunState& state_;
  bool collect_metrics_{};
  Clock::time_point last_preview_{};
};

}  // namespace

class ExtractionController::Impl {
 public:
  ~Impl() {
    cancel();
    join();
  }

  bool start(
      std::filesystem::path input_video,
      std::filesystem::path output_directory,
      Config config,
      ProcessOptions options,
      RunOutputOptions output_options) {
    reapFinished();
    if (worker_.joinable()) {
      return false;
    }

    cancellation_ = std::make_unique<CancellationToken>();
    worker_done_.store(false, std::memory_order_relaxed);
    {
      std::lock_guard lock{state_.mutex};
      state_.snapshot = RunSnapshot{
          .phase = RunPhase::running,
          .status = "Opening video..."};
    }

    auto* const cancellation = cancellation_.get();
    try {
      worker_ = std::thread{
        [this,
         input_video = std::move(input_video),
         output_directory = std::move(output_directory),
         config = std::move(config),
         options = std::move(options),
         output_options,
         cancellation] {
          const auto worker_started = Clock::now();
          try {
            const bool adaptive_selection = !options.fixed_frame_interval;
            GuiRunBridge bridge{state_, adaptive_selection};
            const auto result = runExtraction(
                ExtractionRunRequest{
                    input_video,
                    output_directory,
                    std::move(config),
                    std::move(options),
                    output_options},
                &bridge,
                cancellation,
                &bridge,
                &bridge,
                [this](const RunPaths& paths) {
                  std::lock_guard lock{state_.mutex};
                  state_.snapshot.run_directory = pathToUtf8(paths.run_dir);
                });
            std::lock_guard lock{state_.mutex};
            if (result.status == ExtractionRunStatus::failed) {
              state_.snapshot.phase = RunPhase::failed;
              state_.snapshot.status = "Extraction failed.";
              state_.snapshot.error = result.error;
            } else if (result.status == ExtractionRunStatus::cancelled) {
              state_.snapshot.phase = RunPhase::cancelled;
              state_.snapshot.status = result.outputs_finalized
                  ? "Cancelled. Completed keyframes were saved."
                  : "Cancelled before any keyframes were saved.";
            } else {
              state_.snapshot.phase = RunPhase::complete;
              state_.snapshot.status = "Finished successfully.";
            }
            if (!result.output_paths) {
              state_.snapshot.run_directory.clear();
            }
            state_.snapshot.outcome = RunOutcome{
                result.runtime.count(),
                result.processing.processed_frames,
                result.processing.selected_frames.size(),
                result.outputs_finalized};
          } catch (...) {
            std::lock_guard lock{state_.mutex};
            state_.snapshot.phase = RunPhase::failed;
            state_.snapshot.status = "Extraction failed.";
            state_.snapshot.error = "Unknown controller failure";
            state_.snapshot.outcome = RunOutcome{
                std::chrono::duration<double>(Clock::now() - worker_started).count(),
                state_.snapshot.processed_frames,
                state_.snapshot.selected_keyframes,
                false};
          }
          worker_done_.store(true, std::memory_order_release);
          }};
    } catch (const std::exception& error) {
      cancellation_.reset();
      worker_done_.store(true, std::memory_order_relaxed);
      std::lock_guard lock{state_.mutex};
      state_.snapshot.phase = RunPhase::failed;
      state_.snapshot.status = "Could not start extraction.";
      state_.snapshot.error = error.what();
      state_.snapshot.outcome = RunOutcome{};
      return false;
    }
    return true;
  }

  void cancel() {
    if (cancellation_ == nullptr) {
      return;
    }
    cancellation_->requestCancellation();
    std::lock_guard lock{state_.mutex};
    if (state_.snapshot.phase == RunPhase::running) {
      state_.snapshot.phase = RunPhase::cancelling;
      state_.snapshot.status = "Cancelling after the current frame...";
    }
  }

  void reset() {
    reapFinished();
    if (worker_.joinable()) {
      {
        std::lock_guard lock{state_.mutex};
        if (isActive(state_.snapshot.phase)) {
          return;
        }
      }
      worker_.join();
      cancellation_.reset();
    }
    std::lock_guard lock{state_.mutex};
    state_.snapshot = RunSnapshot{
        .phase = RunPhase::idle,
        .status = "Ready."};
  }

  RunSnapshot takeSnapshot() {
    reapFinished();
    std::lock_guard lock{state_.mutex};
    auto preview = std::exchange(state_.snapshot.preview, {});
    auto metric_samples = std::exchange(state_.snapshot.metric_samples, {});
    auto thumbnails = std::exchange(state_.snapshot.thumbnails, {});
    RunSnapshot result = state_.snapshot;
    result.preview = std::move(preview);
    result.metric_samples = std::move(metric_samples);
    result.thumbnails = std::move(thumbnails);
    return result;
  }

  void join() {
    if (worker_.joinable()) {
      worker_.join();
    }
  }

 private:
  void reapFinished() {
    if (worker_.joinable() && worker_done_.load(std::memory_order_acquire)) {
      worker_.join();
      cancellation_.reset();
    }
  }

  SharedRunState state_;
  std::unique_ptr<CancellationToken> cancellation_;
  std::thread worker_;
  std::atomic_bool worker_done_{false};
};

ExtractionController::ExtractionController() : impl_{std::make_unique<Impl>()} {}
ExtractionController::~ExtractionController() = default;

bool ExtractionController::start(
    std::filesystem::path input_video,
    std::filesystem::path output_directory,
    Config config,
    ProcessOptions options,
    RunOutputOptions output_options) {
  if (options.input_label.empty()) {
    options.input_label = input_video.string();
  }
  return impl_->start(
      std::move(input_video),
      std::move(output_directory),
      std::move(config),
      std::move(options),
      output_options);
}

void ExtractionController::cancel() { impl_->cancel(); }
void ExtractionController::reset() { impl_->reset(); }
RunSnapshot ExtractionController::takeSnapshot() { return impl_->takeSnapshot(); }
void ExtractionController::join() { impl_->join(); }

}  // namespace frame_extractor::gui
