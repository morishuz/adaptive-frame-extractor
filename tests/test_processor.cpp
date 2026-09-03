#include "frame_extractor/processor.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace fe = frame_extractor;

namespace {

class FakeFrameSource final : public fe::FrameSource {
 public:
  explicit FakeFrameSource(
      std::size_t frame_count,
      bool seekable = false,
      int width = 64,
      int height = 48,
      bool seek_succeeds = true)
      : seekable_{seekable}, seek_succeeds_{seek_succeeds} {
    info_.width = width;
    info_.height = height;
    info_.time_base = {1, 10};
    info_.average_frame_rate = {10, 1};
    info_.reported_frame_count = static_cast<std::int64_t>(frame_count);
    info_.codec_name = "fake";
    info_.exact_frame_indices_after_seek = seekable;
    for (std::size_t index = 0; index < frame_count; ++index) {
      cv::Mat image(height, width, CV_8UC3, cv::Scalar{
          static_cast<double>(index),
          static_cast<double>(index),
          static_cast<double>(index)});
      frames_.push_back(fe::DecodedFrame{
          image.clone(),
          static_cast<std::int64_t>(index),
          static_cast<std::int64_t>(index),
          {1, 10}});
    }
  }

  const fe::VideoInfo& info() const override { return info_; }

  std::optional<fe::DecodedFrame> read() override {
    if (next_ >= frames_.size()) {
      return std::nullopt;
    }
    ++read_count_;
    return frames_[next_++];
  }

  bool seekToSeconds(double seconds) override {
    if (!seekable_) {
      return false;
    }
    seek_times_.push_back(seconds);
    if (!seek_succeeds_) {
      return false;
    }
    next_ = 0U;
    while (next_ < frames_.size()
           && frames_[next_].ptsSeconds().value_or(0.0) + 1.0e-9 < seconds) {
      ++next_;
    }
    return true;
  }

  [[nodiscard]] std::size_t reads() const { return read_count_; }
  [[nodiscard]] const std::vector<double>& seekTimes() const { return seek_times_; }

  void clearPresentationTimestamps() {
    for (auto& frame : frames_) {
      frame.pts.reset();
    }
  }

 private:
  fe::VideoInfo info_{};
  std::vector<fe::DecodedFrame> frames_{};
  std::size_t next_{};
  std::size_t read_count_{};
  bool seekable_{};
  bool seek_succeeds_{true};
  std::vector<double> seek_times_;
};

class RecordingObserver : public fe::DiagnosticObserver {
 public:
  void onEvent(const fe::DiagnosticEvent& event) override { events.push_back(event); }
  std::vector<fe::DiagnosticEvent> events;
};

class CancellingObserver final : public RecordingObserver {
 public:
  CancellingObserver(fe::CancellationToken& token, std::size_t cancel_after_processed_index)
      : token_{token}, cancel_after_{cancel_after_processed_index} {}

  void onEvent(const fe::DiagnosticEvent& event) override {
    RecordingObserver::onEvent(event);
    if (const auto* analyzed = std::get_if<fe::FrameAnalyzedEvent>(&event);
        analyzed != nullptr && analyzed->processed_index == cancel_after_) {
      token_.requestCancellation();
    }
  }

 private:
  fe::CancellationToken& token_;
  std::size_t cancel_after_{};
};

class RecordingSink final : public fe::SelectedFrameSink {
 public:
  explicit RecordingSink(const FakeFrameSource& source) : source_{source} {}

  void onFrameSelected(const fe::SelectedFrame& selected, const cv::Mat& frame_bgr) override {
    selections.push_back(selected);
    reads_at_selection.push_back(source_.reads());
    first_pixels.push_back(frame_bgr.at<cv::Vec3b>(0, 0));
  }

  void onSelectionUpdated(const fe::SelectedFrame& selected) override {
    updates.push_back(selected);
  }

  const FakeFrameSource& source_;
  std::vector<fe::SelectedFrame> selections;
  std::vector<fe::SelectedFrame> updates;
  std::vector<std::size_t> reads_at_selection;
  std::vector<cv::Vec3b> first_pixels;
};

class RecordingPreviewSink final : public fe::FramePreviewSink {
 public:
  void onFrameAnalyzed(
      const fe::FrameAnalyzedEvent& analyzed,
      const cv::Mat& frame_bgr,
      const fe::TrackingState& tracking,
      const fe::FlowStepDiagnostics& diagnostics,
      int tracking_frame_width,
      int tracking_frame_height) override {
    processed_indices.push_back(analyzed.processed_index);
    decoded_indices.push_back(analyzed.scores.frame_index);
    first_pixels.push_back(frame_bgr.at<cv::Vec3b>(0, 0));
    tracking_point_counts.push_back(tracking.current_points.size());
    diagnostics_point_counts.push_back(diagnostics.in_bounds_mask.size());
    tracking_sizes.emplace_back(tracking_frame_width, tracking_frame_height);
  }

  std::vector<std::size_t> processed_indices;
  std::vector<std::int64_t> decoded_indices;
  std::vector<cv::Vec3b> first_pixels;
  std::vector<std::size_t> tracking_point_counts;
  std::vector<std::size_t> diagnostics_point_counts;
  std::vector<std::pair<int, int>> tracking_sizes;
};

class DeferredFrameSource final : public fe::FrameSource {
 public:
  explicit DeferredFrameSource(std::size_t frame_count) : frame_count_{frame_count} {
    info_.width = 64;
    info_.height = 48;
    info_.time_base = {1, 10};
    info_.average_frame_rate = {10, 1};
    info_.reported_frame_count = static_cast<std::int64_t>(frame_count);
  }

  const fe::VideoInfo& info() const override { return info_; }

  std::optional<fe::DecodedFrame> read() override {
    if (next_ >= frame_count_) {
      return std::nullopt;
    }
    const auto index = next_++;
    fe::DecodedFrame frame;
    frame.decoded_frame_index = static_cast<std::int64_t>(index);
    frame.pts = static_cast<std::int64_t>(index);
    frame.time_base = info_.time_base;
    frame.render_bgr = [this, index] {
      ++full_render_count_;
      return cv::Mat(
          48, 64, CV_8UC3, cv::Scalar::all(static_cast<double>(index))).clone();
    };
    frame.render_preview_bgr = [this, index](int, int) {
      ++preview_render_count_;
      return cv::Mat(
          24, 32, CV_8UC3, cv::Scalar::all(static_cast<double>(index))).clone();
    };
    return frame;
  }

  [[nodiscard]] std::size_t fullRenderCount() const { return full_render_count_; }
  [[nodiscard]] std::size_t previewRenderCount() const { return preview_render_count_; }

 private:
  fe::VideoInfo info_{};
  std::size_t frame_count_{};
  std::size_t next_{};
  std::size_t full_render_count_{};
  std::size_t preview_render_count_{};
};

class DiscardingSelectionSink final : public fe::SelectedFrameSink {
 public:
  void onFrameSelected(const fe::SelectedFrame&, const cv::Mat&) override {}
};

class BoundedPreviewSink final : public fe::FramePreviewSink {
 public:
  std::optional<cv::Size> requestedFrameSize(
      const fe::FrameAnalyzedEvent&) override {
    return cv::Size{32, 24};
  }

  void onFrameAnalyzed(
      const fe::FrameAnalyzedEvent&,
      const cv::Mat& frame_bgr,
      const fe::TrackingState&,
      const fe::FlowStepDiagnostics&,
      int,
      int) override {
    preview_sizes.push_back(frame_bgr.size());
  }

  std::vector<cv::Size> preview_sizes;
};

fe::Config testConfig() {
  fe::Config config;
  config.sampling.grid_step_analysis_px = 16;
  config.sampling.min_margin_analysis_px = 8;
  config.trigger.main_threshold_analysis_px = 1000.0;
  config.trigger.min_in_bounds_ratio = 0.1;
  config.trigger.max_frames_since_keyframe = 0;
  return config;
}

}  // namespace

TEST_CASE("processor honors start and maximum frame bounds") {
  FakeFrameSource source{6};
  RecordingObserver observer;
  const auto result = fe::processFrames(
      source, testConfig(), {"fixture.mov", 2, 3}, &observer);

  CHECK(source.reads() == 5U);
  CHECK(result.processed_frames == 3U);
  CHECK(result.timings.source_frames_read == 5U);
  CHECK(result.timings.source_read_seconds >= 0.0);
  CHECK(result.timings.analysis_preparation_seconds >= 0.0);
  CHECK(result.timings.dense_flow_seconds >= 0.0);
  CHECK(result.timings.point_sampling_seconds >= 0.0);
  CHECK(result.timings.scoring_seconds >= 0.0);
  CHECK(result.trigger_count == 0U);
  REQUIRE(result.processed_regions.size() == 1U);
  CHECK_FALSE(result.processed_regions[0].requested_range);
  CHECK(result.processed_regions[0].first_decoded_frame_index == 2);
  CHECK(result.processed_regions[0].last_decoded_frame_index == 4);
  CHECK(result.processed_regions[0].processed_frames == 3U);
  CHECK(result.processed_regions[0].keyframes == 2U);
  REQUIRE(result.selected_frames.size() == 2U);
  CHECK(result.selected_frames[0].processed_index == 0U);
  CHECK(result.selected_frames[0].decoded_frame_index == 2);
  CHECK(result.selected_frames[0].selection_reason == "first");
  CHECK(result.selected_frames[1].processed_index == 2U);
  CHECK(result.selected_frames[1].decoded_frame_index == 4);
  CHECK(result.selected_frames[1].selection_reason == "final");

  REQUIRE(std::holds_alternative<fe::RunStartedEvent>(observer.events.front()));
  const auto& started = std::get<fe::RunStartedEvent>(observer.events.front());
  CHECK(started.input_path == "fixture.mov");
  REQUIRE(started.total_frames);
  CHECK(*started.total_frames == 3U);
  REQUIRE(std::holds_alternative<fe::RunFinishedEvent>(observer.events.back()));
}

TEST_CASE("single frame is selected once as both boundaries") {
  FakeFrameSource source{1};
  RecordingObserver observer;
  const auto result = fe::processFrames(source, testConfig(), {}, &observer);
  REQUIRE(result.selected_frames.size() == 1U);
  CHECK(result.selected_frames[0].selection_reason == "first+final");

  bool observed_update = false;
  for (const auto& event : observer.events) {
    if (const auto* updated = std::get_if<fe::KeyframeUpdatedEvent>(&event)) {
      observed_update = updated->keyframe_index == 0U
          && updated->selection_reason == "first+final";
    }
  }
  CHECK(observed_update);
}

TEST_CASE("triggered final frame is deduplicated and preserves causes") {
  FakeFrameSource source{3};
  auto config = testConfig();
  config.trigger.max_frames_since_keyframe = 2;
  const auto result = fe::processFrames(source, config);
  CHECK(result.trigger_count == 1U);
  REQUIRE(result.selected_frames.size() == 2U);
  CHECK(result.selected_frames[1].processed_index == 2U);
  CHECK(result.selected_frames[1].selection_reason == "interval+final");
}

TEST_CASE("EOF before max frames returns every available frame") {
  FakeFrameSource source{2};
  const auto result = fe::processFrames(source, testConfig(), {"", 0, 10});
  CHECK(result.processed_frames == 2U);
  CHECK(source.reads() == 2U);
  REQUIRE(result.selected_frames.size() == 2U);
  CHECK(result.selected_frames.back().decoded_frame_index == 1);
}

TEST_CASE("cancellation finalizes the last analyzed frame") {
  FakeFrameSource source{6};
  fe::CancellationToken token;
  CancellingObserver observer{token, 1};
  const auto result = fe::processFrames(source, testConfig(), {}, &observer, &token);
  CHECK(result.cancelled);
  CHECK(result.processed_frames == 2U);
  REQUIRE(result.selected_frames.size() == 2U);
  CHECK(result.selected_frames.back().processed_index == 1U);
  CHECK(result.selected_frames.back().selection_reason == "final");
  const auto& finished = std::get<fe::RunFinishedEvent>(observer.events.back());
  CHECK(finished.cancelled);
}

TEST_CASE("invalid and unreachable ranges fail clearly") {
  FakeFrameSource source{2};
  CHECK_THROWS_AS(
      fe::processFrames(source, testConfig(), {"", 0, 0}),
      std::invalid_argument);

  FakeFrameSource short_source{2};
  CHECK_THROWS_AS(
      fe::processFrames(short_source, testConfig(), {"", 3, std::nullopt}),
      std::runtime_error);
}

TEST_CASE("selected frames are delivered synchronously without image retention") {
  FakeFrameSource source{3};
  RecordingSink sink{source};
  auto config = testConfig();
  config.trigger.max_frames_since_keyframe = 2;

  const auto result = fe::processFrames(
      source, config, {}, nullptr, nullptr, &sink);

  REQUIRE(sink.selections.size() == 2U);
  CHECK(sink.reads_at_selection[0] == 1U);
  CHECK(sink.first_pixels[0] == cv::Vec3b{0, 0, 0});
  CHECK(sink.reads_at_selection[1] == 3U);
  CHECK(sink.first_pixels[1] == cv::Vec3b{2, 2, 2});
  REQUIRE(sink.updates.size() == 1U);
  CHECK(sink.updates[0].selection_reason == "interval+final");
  CHECK(result.selected_frames.size() == sink.updates[0].keyframe_index + 1U);
}

TEST_CASE("preview frames are delivered synchronously for every analyzed frame") {
  FakeFrameSource source{4};
  RecordingPreviewSink preview;

  const auto result = fe::processFrames(
      source, testConfig(), {}, nullptr, nullptr, nullptr, &preview);

  CHECK(result.processed_frames == 4U);
  CHECK(preview.processed_indices == std::vector<std::size_t>{0U, 1U, 2U, 3U});
  CHECK(preview.decoded_indices == std::vector<std::int64_t>{0, 1, 2, 3});
  CHECK(preview.first_pixels == std::vector<cv::Vec3b>{
      {0, 0, 0}, {1, 1, 1}, {2, 2, 2}, {3, 3, 3}});
  CHECK(preview.tracking_point_counts == preview.diagnostics_point_counts);
  CHECK(preview.tracking_sizes == std::vector<std::pair<int, int>>(4U, {64, 48}));
}

TEST_CASE("selected frames reuse materialized BGR for their bounded preview") {
  DeferredFrameSource source{3};
  DiscardingSelectionSink selection;
  BoundedPreviewSink preview;
  RecordingObserver observer;
  fe::ProcessOptions options;
  options.fixed_frame_interval = 2U;

  const auto result = fe::processFrames(
      source, testConfig(), options, &observer, nullptr, &selection, &preview);

  CHECK(result.trigger_count == 1U);
  CHECK(source.fullRenderCount() == 2U);
  CHECK(source.previewRenderCount() == 1U);
  CHECK(preview.preview_sizes == std::vector<cv::Size>(3U, {32, 24}));

  std::optional<std::size_t> analyzed_position;
  std::optional<std::size_t> selected_position;
  for (std::size_t index = 0; index < observer.events.size(); ++index) {
    if (const auto* analyzed = std::get_if<fe::FrameAnalyzedEvent>(&observer.events[index]);
        analyzed != nullptr && analyzed->processed_index == 2U) {
      analyzed_position = index;
    }
    if (const auto* selected = std::get_if<fe::KeyframeSelectedEvent>(&observer.events[index]);
        selected != nullptr && selected->keyframe_index == 1U) {
      selected_position = index;
    }
  }
  REQUIRE(analyzed_position);
  REQUIRE(selected_position);
  CHECK(*analyzed_position < *selected_position);
}

TEST_CASE("processor uses target-area analysis sizing without upscaling") {
  RecordingPreviewSink large_preview;
  FakeFrameSource large_source{1, false, 3840, 2160};
  const auto large_result = fe::processFrames(
      large_source, fe::Config{}, {}, nullptr, nullptr, nullptr, &large_preview);
  CHECK(large_result.processed_frames == 1U);
  CHECK(large_preview.tracking_sizes == std::vector<std::pair<int, int>>{{960, 540}});
  CHECK(large_preview.tracking_point_counts == std::vector<std::size_t>{336U});

  RecordingPreviewSink small_preview;
  FakeFrameSource small_source{1, false, 960, 480};
  const auto small_result = fe::processFrames(
      small_source, fe::Config{}, {}, nullptr, nullptr, nullptr, &small_preview);
  CHECK(small_result.processed_frames == 1U);
  CHECK(small_preview.tracking_sizes == std::vector<std::pair<int, int>>{{960, 480}});
  CHECK(small_preview.tracking_point_counts == std::vector<std::size_t>{288U});
}

TEST_CASE("time ranges are normalized and processed as independent regions") {
  const auto normalized = fe::normalizeTimeRanges(std::vector<fe::TimeRange>{
      {0.7, 0.8}, {0.2, 0.3}, {0.29, 0.4}});
  CHECK(normalized == std::vector<fe::TimeRange>{{0.2, 0.4}, {0.7, 0.8}});

  FakeFrameSource source{10};
  fe::ProcessOptions options;
  options.input_label = "regions.mov";
  options.regions = {{0.2, 0.3}, {0.7, 0.8}};
  RecordingObserver observer;
  const auto result = fe::processFrames(source, testConfig(), options, &observer);

  CHECK(result.processed_frames == 4U);
  CHECK(result.regions_processed == 2U);
  REQUIRE(result.processed_regions.size() == 2U);
  REQUIRE(result.processed_regions[0].requested_range);
  CHECK(*result.processed_regions[0].requested_range == fe::TimeRange{0.2, 0.3});
  CHECK(result.processed_regions[0].first_decoded_frame_index == 2);
  CHECK(result.processed_regions[0].last_decoded_frame_index == 3);
  CHECK(result.processed_regions[0].first_timestamp_seconds == 0.2);
  CHECK(result.processed_regions[0].last_timestamp_seconds == 0.3);
  CHECK(result.processed_regions[0].processed_frames == 2U);
  CHECK(result.processed_regions[0].keyframes == 2U);
  REQUIRE(result.processed_regions[1].requested_range);
  CHECK(*result.processed_regions[1].requested_range == fe::TimeRange{0.7, 0.8});
  CHECK(result.processed_regions[1].first_decoded_frame_index == 7);
  CHECK(result.processed_regions[1].last_decoded_frame_index == 8);
  CHECK(result.processed_regions[1].processed_frames == 2U);
  CHECK(result.processed_regions[1].keyframes == 2U);
  REQUIRE(result.selected_frames.size() == 4U);
  CHECK(result.selected_frames[0].decoded_frame_index == 2);
  CHECK(result.selected_frames[0].selection_reason == "region_start");
  CHECK(result.selected_frames[0].region_index == 0U);
  CHECK(result.selected_frames[1].decoded_frame_index == 3);
  CHECK(result.selected_frames[1].selection_reason == "region_end");
  CHECK(result.selected_frames[2].decoded_frame_index == 7);
  CHECK(result.selected_frames[2].selection_reason == "region_start");
  CHECK(result.selected_frames[2].region_index == 1U);
  CHECK(result.selected_frames[3].decoded_frame_index == 8);
  CHECK(result.selected_frames[3].selection_reason == "region_end");

  std::vector<std::size_t> analyzed_regions;
  for (const auto& event : observer.events) {
    if (const auto* analyzed = std::get_if<fe::FrameAnalyzedEvent>(&event)) {
      analyzed_regions.push_back(analyzed->region_index);
    }
  }
  CHECK(analyzed_regions == std::vector<std::size_t>{0U, 0U, 1U, 1U});
}

TEST_CASE("seekable sources jump directly between selected time regions") {
  FakeFrameSource source{30, true};
  fe::ProcessOptions options;
  options.regions = {{0.5, 0.7}, {2.0, 2.2}};

  const auto result = fe::processFrames(source, testConfig(), options);

  REQUIRE(source.seekTimes().size() == 2U);
  CHECK(source.seekTimes()[0] == 0.5);
  CHECK(source.seekTimes()[1] == 2.0);
  CHECK(source.reads() == 8U);
  REQUIRE(result.selected_frames.size() == 4U);
  CHECK(result.selected_frames[0].decoded_frame_index == 5);
  CHECK(result.selected_frames[1].decoded_frame_index == 7);
  CHECK(result.selected_frames[2].decoded_frame_index == 20);
  CHECK(result.selected_frames[3].decoded_frame_index == 22);
}

TEST_CASE("failed first region seek falls back to a linear scan") {
  FakeFrameSource source{30, true, 64, 48, false};
  fe::ProcessOptions options;
  options.regions = {{0.5, 0.7}};

  const auto result = fe::processFrames(source, testConfig(), options);

  CHECK(source.seekTimes() == std::vector<double>{0.5});
  CHECK(result.processed_frames == 3U);
  REQUIRE(result.processed_regions.size() == 1U);
  CHECK(result.processed_regions.front().first_decoded_frame_index == 5);
  CHECK(result.processed_regions.front().last_decoded_frame_index == 7);
}

TEST_CASE("missing presentation timestamps emit one warning per run") {
  FakeFrameSource source{5};
  source.clearPresentationTimestamps();
  RecordingObserver observer;

  const auto result = fe::processFrames(source, testConfig(), {}, &observer);

  CHECK(result.pts_unavailable_frames == 5U);
  const auto warning_count = std::ranges::count_if(
      observer.events,
      [](const fe::DiagnosticEvent& event) {
        return std::holds_alternative<fe::WarningEvent>(event);
      });
  CHECK(warning_count == 1);
}

TEST_CASE("processing fails when any selected region contains no video frames") {
  FakeFrameSource source{10};
  fe::ProcessOptions options;
  options.regions = {{0.2, 0.3}, {2.0, 2.2}};

  CHECK_THROWS_WITH(
      fe::processFrames(source, testConfig(), options),
      "Selected region 2 contains no video frames (2.000000 to 2.200000 seconds)");
}

TEST_CASE("cancellation permits selected regions to remain unfinished") {
  FakeFrameSource source{10};
  fe::ProcessOptions options;
  options.regions = {{0.2, 0.3}, {0.7, 0.8}};
  fe::CancellationToken cancellation;
  CancellingObserver observer{cancellation, 1U};

  const auto result = fe::processFrames(
      source, testConfig(), options, &observer, &cancellation);

  CHECK(result.cancelled);
  CHECK(result.regions_processed == 1U);
}

TEST_CASE("fixed frame interval selects exact source-frame ratios") {
  FakeFrameSource source{11};
  fe::ProcessOptions options;
  options.fixed_frame_interval = 4U;
  const auto result = fe::processFrames(source, testConfig(), options);

  CHECK(result.processed_frames == 11U);
  CHECK(result.trigger_count == 2U);
  REQUIRE(result.selected_frames.size() == 4U);
  CHECK(result.selected_frames[0].decoded_frame_index == 0);
  CHECK(result.selected_frames[1].decoded_frame_index == 4);
  CHECK(result.selected_frames[2].decoded_frame_index == 8);
  CHECK(result.selected_frames[3].decoded_frame_index == 10);
  CHECK(result.selected_frames[1].selection_reason == "fixed_interval");
  CHECK(result.selected_frames[3].selection_reason == "final");
}

TEST_CASE("invalid time regions and fixed intervals fail clearly") {
  FakeFrameSource source{3};
  fe::ProcessOptions backwards;
  backwards.regions = {{0.2, 0.1}};
  CHECK_THROWS_AS(
      fe::processFrames(source, testConfig(), backwards),
      std::invalid_argument);

  FakeFrameSource second_source{3};
  fe::ProcessOptions invalid_interval;
  invalid_interval.fixed_frame_interval = 0U;
  CHECK_THROWS_AS(
      fe::processFrames(second_source, testConfig(), invalid_interval),
      std::invalid_argument);
}
