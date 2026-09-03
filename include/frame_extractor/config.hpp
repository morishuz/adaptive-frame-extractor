#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace frame_extractor {

enum class DisPreset { ultrafast, fast, medium };
enum class ImageFormat { jpg, png };

struct DisConfig {
  DisPreset preset{DisPreset::ultrafast};
  int finest_scale{1};
  int patch_size{8};
  int patch_stride{8};
  int gradient_descent_iterations{12};
  int variational_refinement_iterations{0};
  bool use_spatial_propagation{true};

  bool operator==(const DisConfig&) const = default;
};

struct SamplingConfig {
  int grid_step_analysis_px{40};
  int min_margin_analysis_px{4};
  double lost_border_analysis_px{6.0};

  bool operator==(const SamplingConfig&) const = default;
};

struct ScoringConfig {
  double percentile{80.0};

  bool operator==(const ScoringConfig&) const = default;
};

struct TriggerConfig {
  int min_frames_since_keyframe{2};
  double main_threshold_analysis_px{100.0};
  double min_in_bounds_ratio{0.40};
  int max_frames_since_keyframe{1000};

  bool operator==(const TriggerConfig&) const = default;
};

struct VisualizationConfig {
  std::array<std::uint8_t, 3> motion_plot_color_rgb{255, 190, 70};
  std::array<std::uint8_t, 3> points_plot_color_rgb{120, 220, 90};
  std::array<std::uint8_t, 3> threshold_line_color_rgb{40, 120, 255};
  std::array<std::uint8_t, 3> trigger_line_color_rgb{255, 0, 0};

  bool operator==(const VisualizationConfig&) const = default;
};

struct OutputConfig {
  ImageFormat image_format{ImageFormat::jpg};

  bool operator==(const OutputConfig&) const = default;
};

struct Config {
  DisConfig dis{};
  SamplingConfig sampling{};
  ScoringConfig scoring{};
  TriggerConfig trigger{};
  VisualizationConfig visualization{};
  OutputConfig output{};
  int target_analysis_area_px{960 * 540};
  double max_step_norm_analysis_px{25.0};

  bool operator==(const Config&) const = default;
};

[[nodiscard]] Config loadConfig(const std::filesystem::path& path);
[[nodiscard]] std::string dumpConfigYaml(const Config& config);
[[nodiscard]] std::string toString(DisPreset preset);
[[nodiscard]] std::string toString(ImageFormat format);

}  // namespace frame_extractor
