#include "model.hpp"
#include "preview_state.hpp"
#include "widgets.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace gui = frame_extractor::gui;
using Catch::Approx;

TEST_CASE("proposed region length is positive and includes both boundary frames") {
  CHECK_FALSE(gui::estimateRegionLength(4.0, 4.0, 24.0));
  CHECK_FALSE(gui::estimateRegionLength(4.0, 3.0, 24.0));

  const auto length = gui::estimateRegionLength(10.0, 12.5, 24.0);
  REQUIRE(length);
  CHECK(length->seconds == Approx(2.5));
  REQUIRE(length->inclusive_frames);
  CHECK(*length->inclusive_frames == 61);
}

TEST_CASE("proposed region length tolerates unavailable frame rate") {
  const auto length = gui::estimateRegionLength(1.0, 2.0, std::nullopt);
  REQUIRE(length);
  CHECK(length->seconds == Approx(1.0));
  CHECK_FALSE(length->inclusive_frames);
}

TEST_CASE("preparing a scrub preview for extraction retains its position") {
  gui::PreviewState preview;
  preview.frame_index = 3727;
  preview.timestamp_seconds = 62.121667;
  preview.frames_since_keyframe = 17;
  preview.tracking_points = {{0.25F, 0.75F}};

  preview.prepareForRun();

  CHECK(preview.frame_index == 3727);
  CHECK(preview.timestamp_seconds == Approx(62.121667));
  CHECK(preview.frames_since_keyframe == 0);
  CHECK(preview.tracking_points.empty());
}

TEST_CASE("bounded GUI histories retain only their newest entries") {
  std::vector<std::size_t> values(gui::thumbnail_history_capacity + 5U);
  for (std::size_t index = 0U; index < values.size(); ++index) {
    values[index] = index;
  }

  gui::trimToMostRecent(values, gui::thumbnail_history_capacity);

  REQUIRE(values.size() == gui::thumbnail_history_capacity);
  CHECK(values.front() == 5U);
}

TEST_CASE("metric history resets at region boundaries") {
  std::vector<gui::MetricSample> history{
      {.frame_index = 10, .region_index = 0},
      {.frame_index = 11, .region_index = 0}};

  const std::vector<gui::MetricSample> next{
      {.frame_index = 30, .region_index = 1},
      {.frame_index = 31, .region_index = 1}};
  gui::appendMetricSamples(history, next);

  REQUIRE(history.size() == 2U);
  CHECK(history[0].frame_index == 30);
  CHECK(history[1].frame_index == 31);
  CHECK(history[0].region_index == 1U);
}

TEST_CASE("metric history keeps only the newest region in a mixed update") {
  std::vector<gui::MetricSample> history;
  const std::vector<gui::MetricSample> samples{
      {.frame_index = 1, .region_index = 0},
      {.frame_index = 2, .region_index = 0},
      {.frame_index = 20, .region_index = 1}};

  gui::appendMetricSamples(history, samples);

  REQUIRE(history.size() == 1U);
  CHECK(history.front().frame_index == 20);
  CHECK(history.front().region_index == 1U);
}

TEST_CASE("run outcomes expose stable completion statistics") {
  const gui::RunOutcome outcome{
      .runtime_seconds = 12.5,
      .processed_frames = 100,
      .selected_keyframes = 4,
      .outputs_finalized = true};

  REQUIRE(outcome.meanFramesPerKeyframe());
  CHECK(*outcome.meanFramesPerKeyframe() == 25.0);
  CHECK(gui::isTerminal(gui::RunPhase::complete));
  CHECK(gui::isTerminal(gui::RunPhase::cancelled));
  CHECK(gui::isTerminal(gui::RunPhase::failed));
  CHECK_FALSE(gui::isTerminal(gui::RunPhase::running));
}

TEST_CASE("successful run heading contains only the output folder name") {
  gui::RunSnapshot snapshot{
      .phase = gui::RunPhase::complete,
      .status = "Finished successfully.",
      .run_directory = "output/20260902_110833"};

  CHECK(
      gui::runResultHeading(snapshot)
      == "Finished successfully: 20260902_110833");

  snapshot.run_directory += "/";
  CHECK(
      gui::runResultHeading(snapshot)
      == "Finished successfully: 20260902_110833");
}

TEST_CASE("non-successful run headings remain unchanged") {
  const gui::RunSnapshot snapshot{
      .phase = gui::RunPhase::failed,
      .status = "Extraction failed.",
      .run_directory = "output/20260902_110833"};

  CHECK(gui::runResultHeading(snapshot) == snapshot.status);
}
