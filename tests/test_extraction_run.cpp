#include "frame_extractor/extraction_run.hpp"
#include "fixture_support.hpp"
#include "extraction_source.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>

namespace fe = frame_extractor;
using frame_extractor::test::MaterializedFixture;

namespace {

std::filesystem::path uniqueOutputDirectory(std::string_view label) {
  return std::filesystem::temp_directory_path()
      / ("frame_extractor_" + std::string{label} + "_"
         + std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
}

std::string readText(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary};
  REQUIRE(input.good());
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("extraction run publishes reports for a partial failure") {
  const MaterializedFixture fixture{"cfr_h264.mp4"};
  const auto output = uniqueOutputDirectory("partial_run");
  fe::ProcessOptions options;
  options.input_label = fixture.path().string();
  options.fixed_frame_interval = 8U;
  options.regions = {{0.25, 5.0 / 12.0}, {99.0, 100.0}};

  const auto result = fe::runExtraction(fe::ExtractionRunRequest{
      fixture.path(), output, fe::Config{}, options, {}});

  CHECK(result.status == fe::ExtractionRunStatus::failed);
  CHECK(result.outputs_finalized);
  CHECK(result.processing.processed_frames > 0U);
  REQUIRE_FALSE(result.processing.selected_frames.empty());
  REQUIRE(result.output_paths);
  CHECK(std::filesystem::is_regular_file(result.output_paths->manifest_path));
  CHECK(std::filesystem::is_regular_file(result.output_paths->summary_path));
  const auto summary = readText(result.output_paths->summary_path);
  CHECK(summary.find("Failed\n") != std::string::npos);
  CHECK(summary.find("Selected region 2 contains no video frames") != std::string::npos);

  std::filesystem::remove_all(output);
}

TEST_CASE("extraction run removes empty output after early cancellation") {
  const MaterializedFixture fixture{"cfr_h264.mp4"};
  const auto output = uniqueOutputDirectory("cancelled_run");
  fe::CancellationToken cancellation;
  cancellation.requestCancellation();
  std::filesystem::path announced_run;

  const auto result = fe::runExtraction(
      fe::ExtractionRunRequest{
          fixture.path(), output, fe::Config{}, fe::ProcessOptions{}, {}},
      nullptr,
      &cancellation,
      nullptr,
      nullptr,
      [&](const fe::RunPaths& paths) { announced_run = paths.run_dir; });

  CHECK(result.status == fe::ExtractionRunStatus::cancelled);
  CHECK_FALSE(result.outputs_finalized);
  CHECK_FALSE(result.output_paths);
  REQUIRE_FALSE(announced_run.empty());
  CHECK_FALSE(std::filesystem::exists(announced_run));

  std::filesystem::remove_all(output);
}

namespace {

class MetadataSource final : public fe::FrameSource {
 public:
  MetadataSource(bool fail, fe::CancellationToken* cancellation)
      : fail_{fail}, cancellation_{cancellation} {
    info_.width = 32;
    info_.height = 24;
    info_.time_base = {1, 1};
    info_.average_frame_rate = {1, 1};
  }

  const fe::VideoInfo& info() const override { return info_; }

  std::optional<fe::DecodedFrame> read() override {
    if (read_count_++ > 0) {
      if (fail_) {
        throw std::runtime_error("injected decode failure");
      }
      if (cancellation_) {
        cancellation_->requestCancellation();
        return fe::DecodedFrame{};
      }
      return std::nullopt;
    }
    info_.hardware_accelerated_decode = true;
    fe::DecodedFrame frame;
    frame.bgr = cv::Mat(24, 32, CV_8UC3, cv::Scalar{20, 80, 140});
    frame.pts = 0;
    frame.time_base = info_.time_base;
    return frame;
  }

 private:
  fe::VideoInfo info_;
  int read_count_{};
  bool fail_{};
  fe::CancellationToken* cancellation_{};
};

}  // namespace

TEST_CASE("run reports retain decoder metadata discovered while processing") {
  bool fail = false;
  bool cancel = false;
  SECTION("completed run") {}
  SECTION("partial failure") { fail = true; }
  SECTION("cancelled run") { cancel = true; }

  const auto output = uniqueOutputDirectory("decoder_metadata");
  fe::CancellationToken cancellation;
  fe::ProcessOptions options;
  options.fixed_frame_interval = 1U;
  const auto result = fe::detail::runExtractionWithSource(
      fe::ExtractionRunRequest{"video.mov", output, fe::Config{}, options, {}},
      [&](const fe::ExtractionRunRequest&) {
        return std::make_unique<MetadataSource>(fail, cancel ? &cancellation : nullptr);
      },
      nullptr, &cancellation);

  CHECK(result.status == (fail ? fe::ExtractionRunStatus::failed
      : cancel ? fe::ExtractionRunStatus::cancelled
               : fe::ExtractionRunStatus::completed));
  REQUIRE(result.video_info);
  CHECK(result.video_info->hardware_accelerated_decode);
  REQUIRE(result.outputs_finalized);
  REQUIRE(result.output_paths);
  const auto summary = readText(result.output_paths->summary_path);
  const auto label = summary.find("Hardware decoding:");
  REQUIRE(label != std::string::npos);
  CHECK(summary.substr(label, summary.find('\n', label) - label).ends_with("Yes"));
  std::filesystem::remove_all(output);
}
