#include "frame_extractor/config.hpp"
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include <filesystem>
#include <fstream>

namespace fe = frame_extractor;

TEST_CASE("canonical medium config uses resolution-independent analysis settings") {
  const auto path = std::filesystem::path{FRAME_EXTRACTOR_SOURCE_DIR}
      / "configs/profiles/medium.yaml";
  const auto config = fe::loadConfig(path);
  CHECK(config.target_analysis_area_px == 518400);
  CHECK(config.max_step_norm_analysis_px == 25.0);
  CHECK(config.dis.preset == fe::DisPreset::ultrafast);
  CHECK(config.dis.patch_size == 8);
  CHECK(config.dis.patch_stride == 8);
  CHECK(config.dis.gradient_descent_iterations == 12);
  CHECK(config.dis.variational_refinement_iterations == 0);
  CHECK(config.dis.use_spatial_propagation);
  CHECK(config.sampling.grid_step_analysis_px == 40);
  CHECK(config.sampling.min_margin_analysis_px == 4);
  CHECK(config.sampling.lost_border_analysis_px == 6.0);
  CHECK(config.scoring.percentile == 80.0);
  CHECK(config.trigger.min_frames_since_keyframe == 2);
  CHECK(config.trigger.max_frames_since_keyframe == 1000);
  CHECK(config.trigger.main_threshold_analysis_px == 100.0);
  CHECK(config.trigger.min_in_bounds_ratio == 0.40);
  CHECK(config.output.image_format == fe::ImageFormat::jpg);
}

TEST_CASE("config serialization round trips all settings") {
  const auto source = std::filesystem::path{FRAME_EXTRACTOR_SOURCE_DIR}
      / "configs/profiles/medium.yaml";
  const auto expected = fe::loadConfig(source);
  const auto temporary = std::filesystem::temp_directory_path() / "frame_extractor_config_roundtrip.yaml";
  {
    std::ofstream output{temporary};
    REQUIRE(output.good());
    output << fe::dumpConfigYaml(expected);
  }
  CHECK(fe::loadConfig(temporary) == expected);
  std::filesystem::remove(temporary);
}

TEST_CASE("config accepts compatible aliases and rejects invalid values") {
  const auto temporary = std::filesystem::temp_directory_path() / "frame_extractor_config_test.yaml";
  {
    std::ofstream output{temporary};
    REQUIRE(output.good());
    output << "dis:\n  use_spatial_propagation: 'off'\n"
              "output:\n  image_format: JPEG\n";
  }
  const auto config = fe::loadConfig(temporary);
  CHECK_FALSE(config.dis.use_spatial_propagation);
  CHECK(config.output.image_format == fe::ImageFormat::jpg);

  {
    std::ofstream output{temporary};
    REQUIRE(output.good());
    output << "scoring:\n  percentile: 101\n";
  }
  CHECK_THROWS_AS(fe::loadConfig(temporary), std::invalid_argument);

  {
    std::ofstream output{temporary};
    REQUIRE(output.good());
    output << "n_downsample: 2\n";
  }
  CHECK_THROWS_AS(fe::loadConfig(temporary), std::invalid_argument);

  {
    std::ofstream output{temporary};
    REQUIRE(output.good());
    output << "target_analysis_area_pixels: 1000\n";
  }
  CHECK_THROWS_WITH(
      fe::loadConfig(temporary),
      "unknown configuration key: target_analysis_area_pixels");

  {
    std::ofstream output{temporary};
    REQUIRE(output.good());
    output << "trigger:\n  main_threshold_analysis_pixels: 100\n";
  }
  CHECK_THROWS_WITH(
      fe::loadConfig(temporary),
      "unknown configuration key: trigger.main_threshold_analysis_pixels");
  std::filesystem::remove(temporary);
}
