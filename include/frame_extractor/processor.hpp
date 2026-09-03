#pragma once

#include "frame_extractor/config.hpp"
#include "frame_extractor/decoder.hpp"
#include "frame_extractor/diagnostics.hpp"
#include "frame_extractor/tracking.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace frame_extractor {

struct TimeRange {
  double start_seconds{};
  double end_seconds{};

  bool operator==(const TimeRange&) const = default;
};

struct ProcessOptions {
  std::string input_label{};
  std::size_t start_frame{};
  std::optional<std::size_t> max_frames{};
  std::vector<TimeRange> regions{};
  std::optional<std::size_t> fixed_frame_interval{};
  std::string selection_profile{"custom"};
};

struct SelectedFrame {
  std::size_t keyframe_index{};
  std::size_t processed_index{};
  std::int64_t decoded_frame_index{};
  std::optional<std::int64_t> pts{};
  Rational time_base{};
  std::string selection_reason;
  std::string timing_status;
  FrameScores scores{};
  std::size_t region_index{};

  [[nodiscard]] std::optional<double> ptsSeconds() const;
};

struct ProcessedRegion {
  std::size_t region_index{};
  std::optional<TimeRange> requested_range{};
  std::size_t first_processed_index{};
  std::size_t last_processed_index{};
  std::int64_t first_decoded_frame_index{};
  std::int64_t last_decoded_frame_index{};
  double first_timestamp_seconds{};
  double last_timestamp_seconds{};
  std::size_t processed_frames{};
  std::size_t keyframes{};
};

class SelectedFrameSink {
 public:
  virtual ~SelectedFrameSink() = default;
  virtual void onFrameSelected(const SelectedFrame& selected, const cv::Mat& frame_bgr) = 0;
  virtual void onSelectionUpdated(const SelectedFrame& selected) { (void)selected; }
};

class FramePreviewSink {
 public:
  virtual ~FramePreviewSink() = default;
  [[nodiscard]] virtual std::optional<cv::Size> requestedFrameSize(
      const FrameAnalyzedEvent& analyzed) {
    (void)analyzed;
    return cv::Size{};
  }
  virtual void onFrameAnalyzed(
      const FrameAnalyzedEvent& analyzed,
      const cv::Mat& frame_bgr,
      const TrackingState& tracking,
      const FlowStepDiagnostics& diagnostics,
      int tracking_frame_width,
      int tracking_frame_height) = 0;
};

struct ProcessingTimings {
  std::size_t source_frames_read{};
  std::size_t analysis_native_luma_frames{};
  std::size_t analysis_ffmpeg_fallback_frames{};
  double source_read_seconds{};
  double packet_decode_seconds{};
  double hardware_transfer_seconds{};
  double pixel_conversion_seconds{};
  double rotation_seconds{};
  double analysis_preparation_seconds{};
  double dense_flow_seconds{};
  double point_sampling_seconds{};
  double scoring_seconds{};
  double keyframe_sink_seconds{};
  double preview_sink_seconds{};
};

struct ProcessingResult {
  std::size_t processed_frames{};
  std::size_t trigger_count{};
  bool cancelled{};
  std::size_t pts_available_frames{};
  std::size_t pts_unavailable_frames{};
  std::size_t pts_non_monotonic_frames{};
  std::size_t regions_processed{};
  std::vector<SelectedFrame> selected_frames{};
  std::vector<ProcessedRegion> processed_regions{};
  ProcessingTimings timings{};
};

[[nodiscard]] std::vector<TimeRange> normalizeTimeRanges(
    std::span<const TimeRange> ranges);

[[nodiscard]] ProcessingResult processFrames(
    FrameSource& source,
    const Config& config,
    const ProcessOptions& options = {},
    DiagnosticObserver* observer = nullptr,
    const CancellationToken* cancellation = nullptr,
    SelectedFrameSink* selected_frame_sink = nullptr,
    FramePreviewSink* frame_preview_sink = nullptr,
    ProcessingResult* checkpoint = nullptr);

}  // namespace frame_extractor
