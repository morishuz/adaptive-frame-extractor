#include "frame_extractor/extraction_run.hpp"
#include "fixture_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
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
