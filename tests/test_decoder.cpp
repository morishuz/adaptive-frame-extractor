#include "frame_extractor/decoder.hpp"
#include "frame_extractor/processor.hpp"
#include "fixture_support.hpp"
#include "video_timing.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace fe = frame_extractor;
using Catch::Approx;
using frame_extractor::test::MaterializedFixture;

TEST_CASE("near-CFR metadata permits stable frame indices after seeking") {
  CHECK(fe::detail::hasStableSeekFrameIndices(
      {328350, 5473}, {60, 1}, 4378, 72.971667));
  CHECK(fe::detail::hasStableSeekFrameIndices(
      {120, 2}, {60, 1}, std::nullopt, std::nullopt));

  CHECK_FALSE(fe::detail::hasStableSeekFrameIndices(
      {125, 7}, {25, 1}, 5, 0.28));
  CHECK_FALSE(fe::detail::hasStableSeekFrameIndices(
      {5999, 100}, {60, 1}, 359940, 6000.0));
}

namespace {

class CancellingObserver final : public fe::DiagnosticObserver {
 public:
  CancellingObserver(fe::CancellationToken& token, std::size_t cancel_at)
      : token_{token}, cancel_at_{cancel_at} {}

  void onEvent(const fe::DiagnosticEvent& event) override {
    if (const auto* analyzed = std::get_if<fe::FrameAnalyzedEvent>(&event);
        analyzed != nullptr && analyzed->processed_index == cancel_at_) {
      token_.requestCancellation();
    }
  }

 private:
  fe::CancellationToken& token_;
  std::size_t cancel_at_{};
};

}  // namespace

TEST_CASE("fixture decoder accepts an output filename without a parent directory") {
  const auto output = std::filesystem::path{
      "frame_extractor_bare_fixture_"
      + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count())
      + ".mp4"};
  const auto encoded = std::filesystem::path{FRAME_EXTRACTOR_SOURCE_DIR}
      / "fixtures/cfr_h264.mp4.b64";

  frame_extractor::test::decodeBase64File(encoded, output);

  CHECK(std::filesystem::is_regular_file(output));
  CHECK(std::filesystem::file_size(output) > 0U);
  std::filesystem::remove(output);
}

TEST_CASE("video metadata exposes validated frame rate and duration estimates") {
  fe::VideoInfo info;
  info.average_frame_rate = {25, 2};
  info.reported_frame_count = 50;
  CHECK(info.framesPerSecond() == Approx(12.5));
  CHECK(info.estimatedDurationSeconds() == Approx(4.0));

  info.duration_seconds = 3.75;
  CHECK(info.estimatedDurationSeconds() == Approx(3.75));

  info.average_frame_rate = {0, 1};
  info.duration_seconds.reset();
  CHECK_FALSE(info.framesPerSecond());
  CHECK_FALSE(info.estimatedDurationSeconds());
}

TEST_CASE("libav decoder preserves VFR presentation order and source time base") {
  const MaterializedFixture fixture{"vfr_timing.mov"};
  fe::VideoDecoder decoder{fixture.path()};
  const auto info = decoder.info();
  CHECK(info.width == 64);
  CHECK(info.height == 48);
  CHECK(info.time_base == fe::Rational{1, 600});
  CHECK(info.average_frame_rate == fe::Rational{125, 7});
  CHECK_FALSE(info.exact_frame_indices_after_seek);
  REQUIRE(info.reported_frame_count);
  CHECK(*info.reported_frame_count == 5);
  CHECK(info.codec_name == "h264");

  const std::vector<std::int64_t> expected_pts{0, 24, 72, 96, 168};
  std::vector<fe::DecodedFrame> frames;
  while (auto frame = decoder.read()) {
    frames.push_back(std::move(*frame));
  }
  REQUIRE(frames.size() == expected_pts.size());
  for (std::size_t index = 0; index < frames.size(); ++index) {
    CHECK(frames[index].decoded_frame_index == static_cast<std::int64_t>(index));
    REQUIRE(frames[index].pts);
    CHECK(*frames[index].pts == expected_pts[index]);
    REQUIRE(frames[index].ptsSeconds());
    CHECK(*frames[index].ptsSeconds()
          == Approx(static_cast<double>(expected_pts[index]) / 600.0).margin(1.0e-12));
    CHECK(frames[index].bgr.type() == CV_8UC3);
    CHECK(frames[index].bgr.cols == 64);
    CHECK(frames[index].bgr.rows == 48);
    CHECK(frames[index].timings.packet_decode_seconds >= 0.0);
    CHECK(frames[index].timings.hardware_transfer_seconds >= 0.0);
    CHECK(frames[index].timings.pixel_conversion_seconds >= 0.0);
    CHECK(frames[index].timings.rotation_seconds >= 0.0);
  }
}

TEST_CASE("libav decoder handles committed H264 and HEVC CFR fixtures") {
  struct Expectation {
    std::string name;
    std::string codec;
  };
  for (const auto& expected : std::vector<Expectation>{
           {"cfr_h264.mp4", "h264"},
           {"cfr_hevc.mp4", "hevc"}}) {
    DYNAMIC_SECTION(expected.name) {
      const MaterializedFixture fixture{expected.name};
      fe::VideoDecoder decoder{fixture.path()};
      const auto info = decoder.info();
      CHECK(info.codec_name == expected.codec);
      CHECK(info.width == 96);
      CHECK(info.height == 64);
      CHECK(info.average_frame_rate == fe::Rational{12, 1});
      CHECK(info.exact_frame_indices_after_seek);
      CHECK(info.rotation_degrees == 0);

      std::size_t count = 0;
      std::optional<std::int64_t> previous_pts;
      while (const auto frame = decoder.read()) {
        CHECK(frame->decoded_frame_index == static_cast<std::int64_t>(count));
        CHECK(frame->bgr.cols == 96);
        CHECK(frame->bgr.rows == 64);
        REQUIRE(frame->pts);
        if (previous_pts) {
          CHECK(*frame->pts > *previous_pts);
        }
        previous_pts = frame->pts;
        ++count;
      }
      CHECK(count == 12U);
    }
  }
}

TEST_CASE("libav decoder scales analysis and defers full BGR conversion") {
  const MaterializedFixture fixture{"cfr_hevc.mp4"};
  fe::VideoDecoder decoder{
      fixture.path(),
      fe::VideoDecoderOptions{
          .target_analysis_area_px = 48 * 32}};

  const auto frame = decoder.read();
  REQUIRE(frame);
  CHECK(frame->analysis_gray.type() == CV_8UC1);
  CHECK(frame->analysis_gray.cols == 48);
  CHECK(frame->analysis_gray.rows == 32);
  CHECK(frame->bgr.empty());

  const auto preview = frame->previewBgr(40, 40);
  CHECK(preview.type() == CV_8UC3);
  CHECK(preview.cols == 40);
  CHECK(preview.rows == 27);
  CHECK(frame->bgr.empty());

  const auto& full = frame->fullBgr();
  CHECK(full.type() == CV_8UC3);
  CHECK(full.cols == 96);
  CHECK(full.rows == 64);
  CHECK(frame->bgr.data == full.data);
}

TEST_CASE("analysis decoder avoids BGR without enlarging small inputs") {
  const MaterializedFixture fixture{"cfr_hevc.mp4"};
  fe::VideoDecoder decoder{
      fixture.path(),
      fe::VideoDecoderOptions{
          .target_analysis_area_px = 192 * 128}};

  const auto frame = decoder.read();
  REQUIRE(frame);
  CHECK(frame->analysis_gray.type() == CV_8UC1);
  CHECK(frame->analysis_gray.cols == 96);
  CHECK(frame->analysis_gray.rows == 64);
  CHECK(frame->bgr.empty());
}

TEST_CASE("automatic analysis records native luma routing for YUV input") {
  const MaterializedFixture fixture{"cfr_hevc.mp4"};
  fe::VideoDecoder decoder{
      fixture.path(),
      fe::VideoDecoderOptions{
          .target_analysis_area_px = 48 * 32}};

  const auto frame = decoder.read();
  REQUIRE(frame);
  CHECK(
      frame->timings.analysis_conversion_method
      == fe::AnalysisConversionMethod::opencv_native_luma);
}

TEST_CASE("automatic analysis records FFmpeg fallback for RGB input") {
  const MaterializedFixture fixture{"rgb_ffv1.mkv"};
  fe::VideoDecoder decoder{
      fixture.path(),
      fe::VideoDecoderOptions{
          .target_analysis_area_px = 16 * 12}};

  const auto frame = decoder.read();
  REQUIRE(frame);
  CHECK(frame->analysis_gray.size() == cv::Size{16, 12});
  CHECK(
      frame->timings.analysis_conversion_method
      == fe::AnalysisConversionMethod::ffmpeg_fallback);
}

TEST_CASE("automatic analysis rejects packed and interleaved luma layouts") {
  for (const auto& name : std::vector<std::string>{
           "packed_yuyv422.avi", "gray_alpha_ffv1.mkv"}) {
    DYNAMIC_SECTION(name) {
      const MaterializedFixture fixture{name};
      fe::VideoDecoder decoder{
          fixture.path(),
          fe::VideoDecoderOptions{
              .target_analysis_area_px = 16 * 12}};

      const auto frame = decoder.read();
      REQUIRE(frame);
      CHECK(frame->analysis_gray.type() == CV_8UC1);
      CHECK(frame->analysis_gray.size() == cv::Size{16, 12});
      CHECK(
          frame->timings.analysis_conversion_method
          == fe::AnalysisConversionMethod::ffmpeg_fallback);
    }
  }
}

TEST_CASE("automatic analysis preserves native planar high-bit-depth luma") {
  std::vector<cv::Mat> analysis_frames;
  for (const auto& name : std::vector<std::string>{
           "planar_10bit_ffv1.mkv",
           "planar_12bit_ffv1.mkv",
           "planar_16bit_ffv1.mkv"}) {
    INFO(name);
    const MaterializedFixture fixture{name};
    fe::VideoDecoder decoder{
        fixture.path(),
        fe::VideoDecoderOptions{
            .target_analysis_area_px = 16 * 12}};

    const auto frame = decoder.read();
    REQUIRE(frame);
    CHECK(frame->analysis_gray.type() == CV_8UC1);
    CHECK(frame->analysis_gray.size() == cv::Size{16, 12});
    CHECK(
        frame->timings.analysis_conversion_method
        == fe::AnalysisConversionMethod::opencv_native_luma);

    fe::VideoDecoder reference_decoder{
        fixture.path(),
        fe::VideoDecoderOptions{
            .target_analysis_area_px = 32 * 24}};
    const auto reference_frame = reference_decoder.read();
    REQUIRE(reference_frame);
    CHECK(
        reference_frame->timings.analysis_conversion_method
        == fe::AnalysisConversionMethod::ffmpeg_fallback);
    cv::Mat expected;
    cv::resize(
        reference_frame->analysis_gray,
        expected,
        cv::Size{16, 12},
        0.0,
        0.0,
        cv::INTER_AREA);
    CHECK(cv::norm(frame->analysis_gray, expected, cv::NORM_INF) <= 1.0);
    analysis_frames.push_back(frame->analysis_gray.clone());
  }

  REQUIRE(analysis_frames.size() == 3U);
  CHECK(cv::norm(analysis_frames[0], analysis_frames[1], cv::NORM_INF) <= 1.0);
  CHECK(cv::norm(analysis_frames[0], analysis_frames[2], cv::NORM_INF) <= 1.0);
}

TEST_CASE("decoder can defer color conversion without adaptive analysis") {
  const MaterializedFixture fixture{"cfr_hevc.mp4"};
  fe::VideoDecoder decoder{
      fixture.path(),
      fe::VideoDecoderOptions{.defer_full_bgr = true}};

  const auto frame = decoder.read();
  REQUIRE(frame);
  CHECK(frame->analysis_gray.empty());
  CHECK(frame->bgr.empty());

  const auto preview = frame->previewBgr(40, 40);
  CHECK(preview.type() == CV_8UC3);
  CHECK(preview.cols == 40);
  CHECK(preview.rows == 27);
  CHECK(frame->bgr.empty());

  const auto& full = frame->fullBgr();
  CHECK(full.type() == CV_8UC3);
  CHECK(full.cols == 96);
  CHECK(full.rows == 64);
}

TEST_CASE("libav decoder seeks to a presentation-time neighborhood") {
  const MaterializedFixture fixture{"cfr_h264.mp4"};
  fe::VideoDecoder decoder{fixture.path()};
  REQUIRE(decoder.info().duration_seconds);
  CHECK(*decoder.info().duration_seconds == Approx(1.0).margin(0.01));

  decoder.seekToSeconds(0.5);
  std::optional<fe::DecodedFrame> target;
  while (auto frame = decoder.read()) {
    if (frame->ptsSeconds() && *frame->ptsSeconds() >= 0.5 - 1.0e-9) {
      target = std::move(frame);
      break;
    }
  }
  REQUIRE(target);
  REQUIRE(target->ptsSeconds());
  CHECK(*target->ptsSeconds() == Approx(0.5).margin(1.0 / 12.0));
  CHECK(target->decoded_frame_index == 6);
}

TEST_CASE("libav decoder preserves odd frame dimensions") {
  const MaterializedFixture fixture{"odd_h264.mp4"};
  fe::VideoDecoder decoder{fixture.path()};
  CHECK(decoder.info().width == 95);
  CHECK(decoder.info().height == 63);
  CHECK(decoder.info().reported_frame_count == 7);
  const auto first = decoder.read();
  REQUIRE(first);
  CHECK(first->bgr.cols == 95);
  CHECK(first->bgr.rows == 63);
}

TEST_CASE("libav decoder applies display-matrix rotation like the Python backend") {
  const MaterializedFixture raw_fixture{"cfr_h264.mp4"};
  const MaterializedFixture rotated_fixture{"rotated_h264.mov"};
  fe::VideoDecoder raw_decoder{raw_fixture.path()};
  fe::VideoDecoder rotated_decoder{rotated_fixture.path()};
  CHECK(rotated_decoder.info().rotation_degrees == 90);
  CHECK(rotated_decoder.info().width == 64);
  CHECK(rotated_decoder.info().height == 96);

  const auto raw = raw_decoder.read();
  const auto rotated = rotated_decoder.read();
  REQUIRE(raw);
  REQUIRE(rotated);
  cv::Mat expected;
  cv::rotate(raw->bgr, expected, cv::ROTATE_90_COUNTERCLOCKWISE);
  REQUIRE(expected.size() == rotated->bgr.size());
  CHECK(cv::countNonZero(expected.reshape(1) != rotated->bgr.reshape(1)) == 0);
}

TEST_CASE("real decoder honors a nonzero bounded frame range") {
  const MaterializedFixture fixture{"cfr_h264.mp4"};
  fe::VideoDecoder decoder{fixture.path()};
  fe::Config config;
  const auto result = fe::processFrames(decoder, config, {fixture.path().string(), 4, 3});
  CHECK(result.processed_frames == 3U);
  REQUIRE(result.selected_frames.size() == 2U);
  CHECK(result.selected_frames.front().decoded_frame_index == 4);
  CHECK(result.selected_frames.back().decoded_frame_index == 6);
}

TEST_CASE("real decoder processes disjoint presentation-time regions") {
  const MaterializedFixture fixture{"cfr_h264.mp4"};
  fe::VideoDecoder decoder{fixture.path()};
  fe::ProcessOptions options;
  options.input_label = fixture.path().string();
  options.regions = {{3.0 / 12.0, 5.0 / 12.0}, {9.0 / 12.0, 10.0 / 12.0}};
  const auto result = fe::processFrames(decoder, fe::Config{}, options);

  CHECK(result.processed_frames == 5U);
  CHECK(result.regions_processed == 2U);
  REQUIRE(result.selected_frames.size() == 4U);
  CHECK(result.selected_frames[0].decoded_frame_index == 3);
  CHECK(result.selected_frames[1].decoded_frame_index == 5);
  CHECK(result.selected_frames[2].decoded_frame_index == 9);
  CHECK(result.selected_frames[3].decoded_frame_index == 10);
}

TEST_CASE("real decoder cancellation finalizes a longer run") {
  const MaterializedFixture fixture{"long_cfr_h264.mp4"};
  fe::VideoDecoder decoder{fixture.path()};
  fe::CancellationToken token;
  CancellingObserver observer{token, 24};
  const auto result = fe::processFrames(
      decoder, fe::Config{}, {fixture.path().string(), 0, std::nullopt}, &observer, &token);
  CHECK(result.cancelled);
  CHECK(result.processed_frames == 25U);
  REQUIRE_FALSE(result.selected_frames.empty());
  CHECK(result.selected_frames.back().processed_index == 24U);
  CHECK(result.selected_frames.back().selection_reason.ends_with("final"));
}
