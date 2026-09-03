#include "extraction_controller.hpp"
#include "fixture_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace fe = frame_extractor;
namespace gui = frame_extractor::gui;
using frame_extractor::test::MaterializedFixture;

namespace {

gui::RunSnapshot waitForTerminal(gui::ExtractionController& controller) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (std::chrono::steady_clock::now() < deadline) {
    auto snapshot = controller.takeSnapshot();
    if (gui::isTerminal(snapshot.phase)) {
      return snapshot;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  FAIL("extraction controller did not finish before the test timeout");
}

std::filesystem::path uniqueOutputDirectory(std::string_view label) {
  return std::filesystem::temp_directory_path()
      / ("frame_extractor_controller_" + std::string{label} + "_"
         + std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
}

}  // namespace

TEST_CASE("extraction controller completes and publishes output") {
  const MaterializedFixture fixture{"cfr_h264.mp4"};
  const auto output = uniqueOutputDirectory("complete");
  gui::ExtractionController controller;
  fe::ProcessOptions options;
  options.fixed_frame_interval = 8U;

  REQUIRE(controller.start(
      fixture.path(), output, fe::Config{}, options, fe::RunOutputOptions{}));
  const auto snapshot = waitForTerminal(controller);

  CHECK(snapshot.phase == gui::RunPhase::complete);
  REQUIRE(snapshot.outcome);
  CHECK(snapshot.outcome->processed_frames > 0U);
  CHECK(snapshot.outcome->selected_keyframes > 0U);
  CHECK(snapshot.outcome->outputs_finalized);
  CHECK(std::filesystem::is_regular_file(
      std::filesystem::path{snapshot.run_directory} / "summary.txt"));

  controller.join();
  std::filesystem::remove_all(output);
}

TEST_CASE("extraction controller reports decoder failures") {
  const auto output = uniqueOutputDirectory("failure");
  gui::ExtractionController controller;

  REQUIRE(controller.start(
      output / "missing.mp4",
      output,
      fe::Config{},
      fe::ProcessOptions{},
      fe::RunOutputOptions{}));
  const auto snapshot = waitForTerminal(controller);

  CHECK(snapshot.phase == gui::RunPhase::failed);
  CHECK(snapshot.status == "Extraction failed.");
  CHECK_FALSE(snapshot.error.empty());
  REQUIRE(snapshot.outcome);
  CHECK_FALSE(snapshot.outcome->outputs_finalized);
  CHECK(snapshot.run_directory.empty());

  controller.join();
  std::filesystem::remove_all(output);
}
