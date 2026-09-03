#include "frame_extractor/processor.hpp"

#include "frame_extractor/image_processing.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>

namespace frame_extractor {
namespace {

using Clock = std::chrono::steady_clock;

double elapsedSeconds(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

bool cancellationRequested(const CancellationToken* token) {
  return token != nullptr && token->isCancellationRequested();
}

cv::Mat prepareAnalysisGray(const DecodedFrame& frame, int target_area_px) {
  if (!frame.analysis_gray.empty()) {
    return frame.analysis_gray;
  }
  return ensureGray(resizeForAnalysis(frame.fullBgr(), target_area_px));
}

void emit(DiagnosticObserver* observer, const DiagnosticEvent& event) {
  if (observer != nullptr) {
    observer->onEvent(event);
  }
}

std::optional<std::size_t> totalFrames(const VideoInfo& info, const ProcessOptions& options) {
  if (!options.regions.empty()) {
    const auto fps = info.framesPerSecond();
    if (!fps) {
      return std::nullopt;
    }
    double duration = 0.0;
    for (const auto& range : options.regions) {
      duration += range.end_seconds - range.start_seconds;
    }
    const auto estimate = static_cast<std::size_t>(
        std::max(
            1.0,
            std::round(duration * *fps + static_cast<double>(options.regions.size()))));
    if (info.reported_frame_count && *info.reported_frame_count >= 0) {
      return std::min(
          estimate, static_cast<std::size_t>(*info.reported_frame_count));
    }
    return estimate;
  }
  std::optional<std::size_t> available;
  if (info.reported_frame_count && *info.reported_frame_count >= 0) {
    const auto count = static_cast<std::size_t>(*info.reported_frame_count);
    available = count > options.start_frame ? count - options.start_frame : 0U;
  }
  if (options.max_frames) {
    return available ? std::min(*available, *options.max_frames) : options.max_frames;
  }
  return available;
}

std::string addReason(std::string existing, const std::string& reason) {
  std::size_t start = 0;
  while (start <= existing.size()) {
    const auto end = existing.find('+', start);
    if (existing.substr(start, end == std::string::npos ? end : end - start) == reason) {
      return existing;
    }
    if (end == std::string::npos) {
      break;
    }
    start = end + 1U;
  }
  if (!existing.empty()) {
    existing += "+";
  }
  existing += reason;
  return existing;
}

std::string observeTiming(
    ProcessingResult& result,
    const DecodedFrame& frame,
    std::optional<std::int64_t>& previous_pts,
    DiagnosticObserver* observer) {
  if (!frame.pts) {
    ++result.pts_unavailable_frames;
    if (result.pts_unavailable_frames == 1U) {
      emit(observer, WarningEvent{
          "Video frames are missing presentation timestamps; further warnings are suppressed"});
    }
    return "warning";
  }
  ++result.pts_available_frames;
  if (previous_pts && *frame.pts <= *previous_pts) {
    ++result.pts_non_monotonic_frames;
    if (result.pts_non_monotonic_frames == 1U) {
      emit(observer, WarningEvent{
          "Video frames have non-monotonic presentation timestamps; further warnings are suppressed"});
    }
    previous_pts = frame.pts;
    return "invalid";
  }
  previous_pts = frame.pts;
  return "ok";
}

ProcessOptions validatedOptions(const ProcessOptions& options) {
  if (options.max_frames && *options.max_frames == 0U) {
    throw std::invalid_argument("max_frames must be > 0");
  }
  if (!options.regions.empty() && (options.start_frame != 0U || options.max_frames)) {
    throw std::invalid_argument(
        "time regions cannot be combined with start_frame or max_frames");
  }
  if (options.fixed_frame_interval && *options.fixed_frame_interval == 0U) {
    throw std::invalid_argument("fixed_frame_interval must be > 0");
  }

  ProcessOptions normalized = options;
  normalized.regions = normalizeTimeRanges(options.regions);
  return normalized;
}

}  // namespace

std::vector<TimeRange> normalizeTimeRanges(std::span<const TimeRange> ranges) {
  std::vector<TimeRange> normalized{ranges.begin(), ranges.end()};
  for (const auto& range : normalized) {
    if (!std::isfinite(range.start_seconds) || !std::isfinite(range.end_seconds)
        || range.start_seconds < 0.0 || range.end_seconds < range.start_seconds) {
      throw std::invalid_argument(
          "time ranges must be finite, non-negative, and ordered");
    }
  }
  std::ranges::sort(normalized, {}, &TimeRange::start_seconds);
  std::vector<TimeRange> merged;
  for (const auto& range : normalized) {
    if (merged.empty() || range.start_seconds > merged.back().end_seconds + 1.0e-9) {
      merged.push_back(range);
    } else {
      merged.back().end_seconds = std::max(merged.back().end_seconds, range.end_seconds);
    }
  }
  return merged;
}

std::optional<double> SelectedFrame::ptsSeconds() const {
  if (!pts) {
    return std::nullopt;
  }
  return time_base.toSeconds(*pts);
}

ProcessingResult processFrames(
    FrameSource& source,
    const Config& config,
    const ProcessOptions& options,
    DiagnosticObserver* observer,
    const CancellationToken* cancellation,
    SelectedFrameSink* selected_frame_sink,
    FramePreviewSink* frame_preview_sink,
    ProcessingResult* checkpoint) {
  const ProcessOptions normalized_options = validatedOptions(options);

  ProcessingResult local_result;
  ProcessingResult& result = checkpoint != nullptr ? *checkpoint : local_result;
  result = {};

  emit(observer, RunStartedEvent{
      normalized_options.input_label,
      totalFrames(source.info(), normalized_options)});
  std::optional<std::int64_t> previous_pts;

  const auto readFrame = [&] {
    const auto started = Clock::now();
    auto frame = source.read();
    const double source_read_seconds = elapsedSeconds(started);
    result.timings.source_read_seconds += source_read_seconds;
    if (frame) {
      ++result.timings.source_frames_read;
      result.timings.packet_decode_seconds += frame->timings.packet_decode_seconds;
      result.timings.hardware_transfer_seconds +=
          frame->timings.hardware_transfer_seconds;
      result.timings.pixel_conversion_seconds += frame->timings.pixel_conversion_seconds;
      result.timings.rotation_seconds += frame->timings.rotation_seconds;
      switch (frame->timings.analysis_conversion_method) {
        case AnalysisConversionMethod::none:
          break;
        case AnalysisConversionMethod::opencv_native_luma:
          ++result.timings.analysis_native_luma_frames;
          break;
        case AnalysisConversionMethod::ffmpeg_fallback:
          ++result.timings.analysis_ffmpeg_fallback_frames;
          break;
      }
    }
    return std::pair{std::move(frame), source_read_seconds};
  };

  for (std::size_t skipped = 0; skipped < normalized_options.start_frame; ++skipped) {
    if (cancellationRequested(cancellation)) {
      result.cancelled = true;
      emit(observer, RunFinishedEvent{0, 0, true});
      return result;
    }
    if (!readFrame().first) {
      throw std::runtime_error("start_frame is beyond the end of the video");
    }
  }

  struct SegmentState {
    std::size_t region_index{};
    std::optional<TimeRange> requested_range;
    std::size_t first_processed_index{};
    std::int64_t first_decoded_frame_index{};
    double first_timestamp_seconds{};
    std::size_t first_keyframe_index{};
    cv::Mat previous_gray;
    TrackingState tracking;
    FlowStepDiagnostics diagnostics;
    cv::Ptr<cv::DISOpticalFlow> solver;
    DecodedFrame current_frame;
    FrameScores current_scores;
    TriggerDecision current_trigger;
    std::string current_timing_status;
    int frames_since_keyframe{};
  };

  std::optional<SegmentState> segment;
  const bool explicit_regions = !normalized_options.regions.empty();
  const bool fixed_selection = normalized_options.fixed_frame_interval.has_value();
  std::size_t next_region_index = 0U;
  bool seekable_regions = explicit_regions
      && source.info().exact_frame_indices_after_seek
      && source.seekToSeconds(normalized_options.regions.front().start_seconds);

  const auto selectCurrent = [&](SegmentState& active, std::string reason) {
    const std::size_t keyframe_index = result.selected_frames.size();
    const std::size_t processed_index = result.processed_frames - 1U;
    result.selected_frames.push_back(SelectedFrame{
        keyframe_index,
        processed_index,
        active.current_frame.decoded_frame_index,
        active.current_frame.pts,
        active.current_frame.time_base,
        std::move(reason),
        active.current_timing_status,
        active.current_scores,
        active.region_index});
    const auto& selected = result.selected_frames.back();
    if (selected_frame_sink != nullptr) {
      const auto started = Clock::now();
      selected_frame_sink->onFrameSelected(selected, active.current_frame.fullBgr());
      result.timings.keyframe_sink_seconds += elapsedSeconds(started);
    }
    emit(observer, KeyframeSelectedEvent{
        selected.keyframe_index,
        selected.processed_index,
        selected.decoded_frame_index,
        selected.selection_reason});
  };

  const auto finalizeSegment = [&] {
    if (!segment) {
      return;
    }
    const std::string boundary_reason = explicit_regions ? "region_end" : "final";
    const std::size_t final_processed_index = result.processed_frames - 1U;
    if (!result.selected_frames.empty()
        && result.selected_frames.back().processed_index == final_processed_index) {
      auto& last = result.selected_frames.back();
      last.selection_reason = addReason(last.selection_reason, boundary_reason);
      if (selected_frame_sink != nullptr) {
        const auto started = Clock::now();
        selected_frame_sink->onSelectionUpdated(last);
        result.timings.keyframe_sink_seconds += elapsedSeconds(started);
      }
      emit(observer, KeyframeUpdatedEvent{last.keyframe_index, last.selection_reason});
    } else {
      selectCurrent(*segment, boundary_reason);
    }
    result.processed_regions.push_back(ProcessedRegion{
        segment->region_index,
        segment->requested_range,
        segment->first_processed_index,
        result.processed_frames - 1U,
        segment->first_decoded_frame_index,
        segment->current_frame.decoded_frame_index,
        segment->first_timestamp_seconds,
        relativeTimestampSeconds(segment->current_frame, source.info()),
        result.processed_frames - segment->first_processed_index,
        result.selected_frames.size() - segment->first_keyframe_index});
    ++result.regions_processed;
    segment.reset();
  };

  const auto publishPreview = [&](const FrameAnalyzedEvent& analyzed) {
    if (frame_preview_sink == nullptr || !segment) {
      return;
    }
    const auto requested_size = frame_preview_sink->requestedFrameSize(analyzed);
    if (!requested_size) {
      return;
    }
    const auto started = Clock::now();
    cv::Mat bounded_preview;
    const cv::Mat* preview_bgr = nullptr;
    if (requested_size->width > 0 && requested_size->height > 0) {
      bounded_preview = segment->current_frame.previewBgr(
          requested_size->width, requested_size->height);
      preview_bgr = &bounded_preview;
    } else {
      preview_bgr = &segment->current_frame.fullBgr();
    }
    const int width = fixed_selection
        ? preview_bgr->cols
        : segment->previous_gray.cols;
    const int height = fixed_selection
        ? preview_bgr->rows
        : segment->previous_gray.rows;
    frame_preview_sink->onFrameAnalyzed(
        analyzed,
        *preview_bgr,
        segment->tracking,
        segment->diagnostics,
        width,
        height);
    result.timings.preview_sink_seconds += elapsedSeconds(started);
  };

  const auto beginSegment = [&](DecodedFrame frame, std::size_t region_index, double decode_seconds) {
    SegmentState active;
    active.region_index = region_index;
    if (explicit_regions) {
      active.requested_range = normalized_options.regions[region_index];
    }
    active.first_processed_index = result.processed_frames;
    active.first_keyframe_index = result.selected_frames.size();
    active.current_frame = std::move(frame);
    const double timestamp = relativeTimestampSeconds(active.current_frame, source.info());
    active.first_decoded_frame_index = active.current_frame.decoded_frame_index;
    active.first_timestamp_seconds = timestamp;
    if (!fixed_selection) {
      const auto analysis_started = Clock::now();
      active.previous_gray = prepareAnalysisGray(
          active.current_frame, config.target_analysis_area_px);
      result.timings.analysis_preparation_seconds += elapsedSeconds(analysis_started);
      active.tracking = initializeTrackingState(
          active.previous_gray.cols, active.previous_gray.rows, config);
      active.diagnostics.in_bounds_mask.assign(
          active.tracking.alive_mask.size(), std::uint8_t{1});
      active.solver = createDisFlow(config.dis);
    }
    const auto initial_points = active.tracking.origin_points.size();
    active.current_scores = FrameScores{
        active.current_frame.decoded_frame_index,
        timestamp,
        0.0,
        initial_points,
        fixed_selection || initial_points > 0U ? 1.0 : 0.0};
    active.current_trigger = TriggerDecision{false, "none", 0};
    active.current_timing_status = observeTiming(
        result, active.current_frame, previous_pts, observer);
    segment = std::move(active);
    ++result.processed_frames;

    selectCurrent(*segment, explicit_regions ? "region_start" : "first");
    const FrameAnalyzedEvent analyzed{
        result.processed_frames - 1U,
        segment->current_scores,
        segment->current_trigger,
        decode_seconds,
        0.0,
        segment->region_index};
    emit(observer, analyzed);
    publishPreview(analyzed);
  };

  const auto processNext = [&](DecodedFrame frame, double decode_seconds) {
    const double timestamp = relativeTimestampSeconds(frame, source.info());
    double flow_seconds = 0.0;
    if (fixed_selection) {
      const bool triggered =
          static_cast<std::size_t>(segment->frames_since_keyframe + 1)
          >= *normalized_options.fixed_frame_interval;
      segment->current_scores = FrameScores{
          frame.decoded_frame_index, timestamp, 0.0, 0U, 1.0};
      segment->current_trigger = TriggerDecision{
          triggered, triggered ? "fixed_interval" : "none", segment->frames_since_keyframe + 1};
    } else {
      const auto analysis_started = Clock::now();
      auto current_gray = prepareAnalysisGray(frame, config.target_analysis_area_px);
      result.timings.analysis_preparation_seconds += elapsedSeconds(analysis_started);
      const auto flow_started = Clock::now();
      TrackingStepTimings tracking_timings;
      segment->diagnostics = stepTracking(
          segment->tracking,
          segment->previous_gray,
          current_gray,
          *segment->solver,
          config,
          &tracking_timings);
      flow_seconds = elapsedSeconds(flow_started);
      result.timings.dense_flow_seconds += tracking_timings.dense_flow_seconds;
      result.timings.point_sampling_seconds += tracking_timings.point_sampling_seconds;
      const auto scoring_started = Clock::now();
      segment->current_scores = computeFrameScores(
          segment->tracking,
          segment->diagnostics,
          frame.decoded_frame_index,
          timestamp,
          config);
      segment->current_trigger = decideTrigger(
          segment->current_scores,
          segment->frames_since_keyframe + 1,
          config.trigger);
      result.timings.scoring_seconds += elapsedSeconds(scoring_started);
      segment->previous_gray = std::move(current_gray);
    }

    segment->current_frame = std::move(frame);
    segment->current_timing_status = observeTiming(
        result, segment->current_frame, previous_pts, observer);
    ++result.processed_frames;
    const FrameAnalyzedEvent analyzed{
        result.processed_frames - 1U,
        segment->current_scores,
        segment->current_trigger,
        decode_seconds,
        flow_seconds,
        segment->region_index};

    emit(observer, analyzed);
    if (segment->current_trigger.triggered && selected_frame_sink != nullptr) {
      const auto started = Clock::now();
      (void)segment->current_frame.fullBgr();
      result.timings.keyframe_sink_seconds += elapsedSeconds(started);
    }
    publishPreview(analyzed);

    if (segment->current_trigger.triggered) {
      ++result.trigger_count;
      selectCurrent(*segment, segment->current_trigger.displayReason());
      if (!fixed_selection) {
        segment->tracking = initializeTrackingState(
            segment->previous_gray.cols, segment->previous_gray.rows, config);
      }
      segment->frames_since_keyframe = 0;
    } else {
      ++segment->frames_since_keyframe;
    }
  };

  while (!normalized_options.max_frames
         || result.processed_frames < *normalized_options.max_frames) {
    if (cancellationRequested(cancellation)) {
      result.cancelled = true;
      break;
    }
    auto [next, decode_seconds] = readFrame();
    if (!next) {
      break;
    }
    if (cancellationRequested(cancellation)) {
      result.cancelled = true;
      break;
    }

    std::size_t frame_region_index = 0U;
    if (explicit_regions) {
      const double timestamp = relativeTimestampSeconds(*next, source.info());
      bool sought_next_region = false;
      while (next_region_index < normalized_options.regions.size()
             && timestamp > normalized_options.regions[next_region_index].end_seconds + 1.0e-9) {
        if (segment && segment->region_index == next_region_index) {
          finalizeSegment();
        }
        ++next_region_index;
        if (seekable_regions && next_region_index < normalized_options.regions.size()
            && timestamp + 1.0e-9
                < normalized_options.regions[next_region_index].start_seconds) {
          if (source.seekToSeconds(
                  normalized_options.regions[next_region_index].start_seconds)) {
            previous_pts.reset();
            sought_next_region = true;
            break;
          }
          seekable_regions = false;
        }
      }
      if (sought_next_region) {
        continue;
      }
      if (next_region_index >= normalized_options.regions.size()) {
        break;
      }
      if (timestamp + 1.0e-9
          < normalized_options.regions[next_region_index].start_seconds) {
        continue;
      }
      frame_region_index = next_region_index;
    } else if (!segment
               && next->decoded_frame_index
                   != static_cast<std::int64_t>(normalized_options.start_frame)) {
      throw std::runtime_error("decoded frame index does not match requested start_frame");
    }

    if (!segment) {
      beginSegment(std::move(*next), frame_region_index, decode_seconds);
    } else if (segment->region_index != frame_region_index) {
      finalizeSegment();
      beginSegment(std::move(*next), frame_region_index, decode_seconds);
    } else {
      processNext(std::move(*next), decode_seconds);
    }
  }

  finalizeSegment();
  if (explicit_regions && !result.cancelled
      && result.processed_regions.size() != normalized_options.regions.size()) {
    for (std::size_t region_index = 0U;
         region_index < normalized_options.regions.size();
         ++region_index) {
      const bool processed = std::ranges::any_of(
          result.processed_regions,
          [region_index](const ProcessedRegion& region) {
            return region.region_index == region_index;
          });
      if (!processed) {
        const auto& region = normalized_options.regions[region_index];
        throw std::runtime_error(
            "Selected region " + std::to_string(region_index + 1U)
            + " contains no video frames ("
            + std::to_string(region.start_seconds) + " to "
            + std::to_string(region.end_seconds) + " seconds)");
      }
    }
  }
  if (result.processed_frames == 0U && !result.cancelled) {
    throw std::runtime_error(
        explicit_regions
            ? "No video frames fall within the selected regions"
            : "Could not read the first frame from the requested range");
  }
  emit(observer, RunFinishedEvent{
      result.processed_frames, result.selected_frames.size(), result.cancelled});
  return result;
}

}  // namespace frame_extractor
