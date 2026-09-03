#include "project_state.hpp"
#include "profile_configs.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fe = frame_extractor;
namespace gui = frame_extractor::gui;

TEST_CASE("bundled GUI profiles preserve the current density ordering") {
  const auto directory = std::filesystem::path{FRAME_EXTRACTOR_SOURCE_DIR}
      / "configs/profiles";
  const auto profiles = gui::ProfileConfigs::load(directory);
  const auto& low = profiles.forSelection(gui::SelectionChoice::low);
  const auto& medium = profiles.forSelection(gui::SelectionChoice::medium);
  const auto& high = profiles.forSelection(gui::SelectionChoice::high);

  CHECK(medium == fe::Config{});
  CHECK(profiles.forSelection(gui::SelectionChoice::fixed) == medium);

  auto expected_low = medium;
  expected_low.trigger.min_frames_since_keyframe = 4;
  expected_low.trigger.max_frames_since_keyframe = 1500;
  expected_low.trigger.main_threshold_analysis_px = 150.0;
  expected_low.trigger.min_in_bounds_ratio = 0.25;
  CHECK(low == expected_low);

  auto expected_high = medium;
  expected_high.trigger.min_frames_since_keyframe = 1;
  expected_high.trigger.max_frames_since_keyframe = 500;
  expected_high.trigger.main_threshold_analysis_px = 62.5;
  expected_high.trigger.min_in_bounds_ratio = 0.55;
  CHECK(high == expected_high);
}

TEST_CASE("per-video GUI project state round trips profiles and regions") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto directory = std::filesystem::temp_directory_path()
      / ("frame_extractor_project_state_" + unique);
  const auto input_video = directory / "example.mov";
  const gui::ProjectStore store{directory / "state"};

  gui::ProjectState saved;
  saved.selection = gui::SelectionChoice::fixed;
  saved.frame_interval = 10;
  saved.image_format = fe::ImageFormat::png;
  saved.group_keyframes_by_region = true;
  saved.playhead_seconds = 12.75;
  saved.regions = {{8.0, 10.0}, {2.0, 4.0}};
  store.save(input_video, saved);

  const auto loaded = store.load(input_video);
  CHECK(loaded.selection == gui::SelectionChoice::fixed);
  CHECK(loaded.frame_interval == 10);
  CHECK(loaded.image_format == fe::ImageFormat::png);
  CHECK(loaded.group_keyframes_by_region);
  CHECK(loaded.playhead_seconds == Catch::Approx(12.75));
  REQUIRE(loaded.regions.size() == 2U);
  CHECK(loaded.regions[0] == fe::TimeRange{2.0, 4.0});
  CHECK(loaded.regions[1] == fe::TimeRange{8.0, 10.0});

  const auto state_file = std::filesystem::directory_iterator{directory / "state"}->path();
  std::ofstream legacy{state_file, std::ios::binary | std::ios::trunc};
  legacy << "selection: fixed\nfixed_fps: 12\nplayhead_seconds: 0\n";
  legacy.close();
  const auto loaded_legacy = store.load(input_video);
  CHECK(loaded_legacy.frame_interval == 12);
  CHECK(loaded_legacy.image_format == fe::ImageFormat::jpg);

  std::ofstream malformed{state_file, std::ios::binary | std::ios::trunc};
  malformed << "regions: [\n";
  malformed.close();
  const auto recovered = store.loadOrDefault(input_video);
  CHECK(recovered.state.selection == gui::SelectionChoice::medium);
  CHECK(recovered.state.frame_interval == 8);
  CHECK(recovered.state.regions.empty());
  CHECK_FALSE(recovered.warning.empty());

  std::filesystem::remove_all(directory);
}

TEST_CASE("GUI project state owns profile and region editing behavior") {
  gui::ProjectState state;
  state.selection = gui::SelectionChoice::fixed;
  state.frame_interval = 8;
  state.image_format = fe::ImageFormat::png;
  state.group_keyframes_by_region = true;
  state.regions = {{8.0, 10.0}};

  const auto options = gui::processOptionsForProject(state, "video.mov");
  CHECK(options.input_label == "video.mov");
  CHECK(options.fixed_frame_interval == 8U);
  CHECK(options.selection_profile == "fixed");
  CHECK(options.regions == state.regions);
  const auto config = gui::configForProject(state, fe::Config{});
  CHECK(config.output.image_format == fe::ImageFormat::png);
  CHECK(gui::outputOptionsForProject(state).group_keyframes_by_region);

  state.regions.clear();
  CHECK_FALSE(gui::outputOptionsForProject(state).group_keyframes_by_region);
  state.regions = {{8.0, 10.0}};

  gui::addRegion(state, 4.0, 2.0);
  REQUIRE(state.regions.size() == 2U);
  CHECK(state.regions[0] == fe::TimeRange{2.0, 4.0});
  CHECK(gui::regionIndexAt(state.regions, 3.0) == 0U);
  CHECK_FALSE(gui::regionIndexAt(state.regions, 6.0));
  CHECK(gui::removeRegionAt(state, 3.0));
  CHECK_FALSE(gui::removeRegionAt(state, 3.0));
  CHECK(state.regions == std::vector<fe::TimeRange>{{8.0, 10.0}});
}
