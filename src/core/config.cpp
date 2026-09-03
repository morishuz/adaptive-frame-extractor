#include "frame_extractor/config.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace frame_extractor {
namespace {

template <typename T>
T scalarOr(const YAML::Node& parent, std::string_view key, T fallback) {
  const auto node = parent[std::string{key}];
  return node ? node.as<T>() : fallback;
}

int integerAtLeast(const YAML::Node& parent, std::string_view key, int fallback, int minimum) {
  const int value = scalarOr<int>(parent, key, fallback);
  if (value < minimum) {
    throw std::invalid_argument(std::string{key} + " must be >= " + std::to_string(minimum));
  }
  return value;
}

double finiteAtLeast(
    const YAML::Node& parent,
    std::string_view key,
    double fallback,
    double minimum) {
  const double value = scalarOr<double>(parent, key, fallback);
  if (!std::isfinite(value) || value < minimum) {
    throw std::invalid_argument(std::string{key} + " must be finite and >= " + std::to_string(minimum));
  }
  return value;
}

YAML::Node mappingOrEmpty(const YAML::Node& root, std::string_view key) {
  const auto node = root[std::string{key}];
  if (!node) {
    return YAML::Node{YAML::NodeType::Map};
  }
  if (!node.IsMap()) {
    throw std::invalid_argument("'" + std::string{key} + "' must be a mapping");
  }
  return node;
}

void validateKeys(
    const YAML::Node& mapping,
    std::string_view path,
    std::initializer_list<std::string_view> allowed_keys) {
  for (const auto& entry : mapping) {
    if (!entry.first.IsScalar()) {
      throw std::invalid_argument(
          std::string{path.empty() ? "configuration" : path}
          + " keys must be strings");
    }
    const auto key = entry.first.Scalar();
    if (std::find(allowed_keys.begin(), allowed_keys.end(), key)
        == allowed_keys.end()) {
      throw std::invalid_argument(
          "unknown configuration key: "
          + (path.empty() ? key : std::string{path} + "." + key));
    }
  }
}

void rejectDeprecatedKey(
    const YAML::Node& parent,
    std::string_view key,
    std::string_view replacement) {
  if (parent[std::string{key}]) {
    throw std::invalid_argument(
        std::string{key} + " is no longer supported; use " + std::string{replacement});
  }
}

std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool booleanOr(const YAML::Node& parent, std::string_view key, bool fallback) {
  const auto node = parent[std::string{key}];
  if (!node) {
    return fallback;
  }
  if (!node.IsScalar()) {
    throw std::invalid_argument(std::string{key} + " must be a boolean");
  }
  const auto value = lowercase(node.Scalar());
  if (value == "true" || value == "yes" || value == "1" || value == "on") {
    return true;
  }
  if (value == "false" || value == "no" || value == "0" || value == "off") {
    return false;
  }
  throw std::invalid_argument(std::string{key} + " must be a boolean");
}

std::array<std::uint8_t, 3> colorOr(
    const YAML::Node& parent,
    std::string_view key,
    const std::array<std::uint8_t, 3>& fallback) {
  const auto node = parent[std::string{key}];
  if (!node) {
    return fallback;
  }
  if (!node.IsSequence() || node.size() != 3U) {
    throw std::invalid_argument(std::string{key} + " must contain three RGB values");
  }
  std::array<std::uint8_t, 3> color{};
  for (std::size_t index = 0; index < color.size(); ++index) {
    const int channel = node[index].as<int>();
    if (channel < 0 || channel > 255) {
      throw std::invalid_argument(std::string{key} + " RGB values must be in [0, 255]");
    }
    color[index] = static_cast<std::uint8_t>(channel);
  }
  return color;
}

DisPreset parseDisPreset(const YAML::Node& parent, DisPreset fallback) {
  const auto node = parent["preset"];
  if (!node) {
    return fallback;
  }
  const auto value = lowercase(node.as<std::string>());
  if (value == "ultrafast") {
    return DisPreset::ultrafast;
  }
  if (value == "fast") {
    return DisPreset::fast;
  }
  if (value == "medium") {
    return DisPreset::medium;
  }
  throw std::invalid_argument("dis.preset must be one of: fast, medium, ultrafast");
}

ImageFormat parseImageFormat(const YAML::Node& parent, ImageFormat fallback) {
  const auto node = parent["image_format"];
  if (!node) {
    return fallback;
  }
  auto value = lowercase(node.as<std::string>());
  if (value == "jpeg") {
    value = "jpg";
  }
  if (value == "jpg") {
    return ImageFormat::jpg;
  }
  if (value == "png") {
    return ImageFormat::png;
  }
  throw std::invalid_argument("output.image_format must be one of: jpg, jpeg, png");
}

YAML::Node colorNode(const std::array<std::uint8_t, 3>& color) {
  YAML::Node result{YAML::NodeType::Sequence};
  for (const auto channel : color) {
    result.push_back(static_cast<unsigned int>(channel));
  }
  result.SetStyle(YAML::EmitterStyle::Flow);
  return result;
}

}  // namespace

Config loadConfig(const std::filesystem::path& path) {
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error("Config file not found: " + path.string());
  }

  std::ifstream input{path, std::ios::binary};
  if (!input.good()) {
    throw std::runtime_error("Cannot read config file: " + path.string());
  }
  const YAML::Node root = YAML::Load(input);
  if (!root.IsMap()) {
    throw std::invalid_argument("'root' must be a mapping");
  }

  Config config{};
  const auto dis = mappingOrEmpty(root, "dis");
  const auto sampling = mappingOrEmpty(root, "sampling");
  const auto scoring = mappingOrEmpty(root, "scoring");
  const auto trigger = mappingOrEmpty(root, "trigger");
  const auto visualization = mappingOrEmpty(root, "visualization");
  const auto output = mappingOrEmpty(root, "output");

  rejectDeprecatedKey(root, "n_downsample", "target_analysis_area_px");
  rejectDeprecatedKey(
      root, "max_step_norm_original_px", "max_step_norm_analysis_px");
  rejectDeprecatedKey(
      sampling, "grid_step_original_px", "sampling.grid_step_analysis_px");
  rejectDeprecatedKey(
      sampling, "min_margin_original_px", "sampling.min_margin_analysis_px");
  rejectDeprecatedKey(
      sampling, "lost_border_original_px", "sampling.lost_border_analysis_px");
  rejectDeprecatedKey(
      trigger,
      "main_threshold_original_px",
      "trigger.main_threshold_analysis_px");

  validateKeys(
      root,
      {},
      {"target_analysis_area_px",
       "max_step_norm_analysis_px",
       "dis",
       "sampling",
       "scoring",
       "trigger",
       "visualization",
       "output"});
  validateKeys(
      dis,
      "dis",
      {"preset",
       "finest_scale",
       "patch_size",
       "patch_stride",
       "gradient_descent_iterations",
       "variational_refinement_iterations",
       "use_spatial_propagation"});
  validateKeys(
      sampling,
      "sampling",
      {"grid_step_analysis_px",
       "min_margin_analysis_px",
       "lost_border_analysis_px"});
  validateKeys(scoring, "scoring", {"percentile"});
  validateKeys(
      trigger,
      "trigger",
      {"min_frames_since_keyframe",
       "main_threshold_analysis_px",
       "min_in_bounds_ratio",
       "max_frames_since_keyframe"});
  validateKeys(
      visualization,
      "visualization",
      {"motion_plot_color_rgb",
       "points_plot_color_rgb",
       "threshold_line_color_rgb",
       "trigger_line_color_rgb"});
  validateKeys(output, "output", {"image_format"});

  config.target_analysis_area_px = integerAtLeast(
      root, "target_analysis_area_px", config.target_analysis_area_px, 1);
  config.max_step_norm_analysis_px = finiteAtLeast(
      root, "max_step_norm_analysis_px", config.max_step_norm_analysis_px, 0.0);

  config.dis.preset = parseDisPreset(dis, config.dis.preset);
  config.dis.finest_scale = integerAtLeast(dis, "finest_scale", config.dis.finest_scale, 0);
  config.dis.patch_size = integerAtLeast(dis, "patch_size", config.dis.patch_size, 1);
  config.dis.patch_stride = integerAtLeast(dis, "patch_stride", config.dis.patch_stride, 1);
  config.dis.gradient_descent_iterations = integerAtLeast(
      dis, "gradient_descent_iterations", config.dis.gradient_descent_iterations, 0);
  config.dis.variational_refinement_iterations = integerAtLeast(
      dis,
      "variational_refinement_iterations",
      config.dis.variational_refinement_iterations,
      0);
  config.dis.use_spatial_propagation = booleanOr(
      dis, "use_spatial_propagation", config.dis.use_spatial_propagation);

  config.sampling.grid_step_analysis_px = integerAtLeast(
      sampling, "grid_step_analysis_px", config.sampling.grid_step_analysis_px, 1);
  config.sampling.min_margin_analysis_px = integerAtLeast(
      sampling, "min_margin_analysis_px", config.sampling.min_margin_analysis_px, 0);
  config.sampling.lost_border_analysis_px = finiteAtLeast(
      sampling, "lost_border_analysis_px", config.sampling.lost_border_analysis_px, 0.0);

  config.scoring.percentile = finiteAtLeast(scoring, "percentile", config.scoring.percentile, 0.0);
  if (config.scoring.percentile > 100.0) {
    throw std::invalid_argument("scoring.percentile must be in [0, 100]");
  }

  config.trigger.min_frames_since_keyframe = integerAtLeast(
      trigger, "min_frames_since_keyframe", config.trigger.min_frames_since_keyframe, 0);
  config.trigger.main_threshold_analysis_px = finiteAtLeast(
      trigger,
      "main_threshold_analysis_px",
      config.trigger.main_threshold_analysis_px,
      0.0);
  config.trigger.min_in_bounds_ratio = finiteAtLeast(
      trigger, "min_in_bounds_ratio", config.trigger.min_in_bounds_ratio, 0.0);
  if (config.trigger.min_in_bounds_ratio <= 0.0 || config.trigger.min_in_bounds_ratio > 1.0) {
    throw std::invalid_argument("trigger.min_in_bounds_ratio must be in (0, 1]");
  }
  config.trigger.max_frames_since_keyframe = integerAtLeast(
      trigger, "max_frames_since_keyframe", config.trigger.max_frames_since_keyframe, 0);

  config.visualization.motion_plot_color_rgb = colorOr(
      visualization, "motion_plot_color_rgb", config.visualization.motion_plot_color_rgb);
  config.visualization.points_plot_color_rgb = colorOr(
      visualization, "points_plot_color_rgb", config.visualization.points_plot_color_rgb);
  config.visualization.threshold_line_color_rgb = colorOr(
      visualization,
      "threshold_line_color_rgb",
      config.visualization.threshold_line_color_rgb);
  config.visualization.trigger_line_color_rgb = colorOr(
      visualization, "trigger_line_color_rgb", config.visualization.trigger_line_color_rgb);

  config.output.image_format = parseImageFormat(output, config.output.image_format);
  return config;
}

std::string dumpConfigYaml(const Config& config) {
  YAML::Node root;
  root["target_analysis_area_px"] = config.target_analysis_area_px;
  root["max_step_norm_analysis_px"] = config.max_step_norm_analysis_px;

  auto dis = root["dis"];
  dis["preset"] = toString(config.dis.preset);
  dis["finest_scale"] = config.dis.finest_scale;
  dis["patch_size"] = config.dis.patch_size;
  dis["patch_stride"] = config.dis.patch_stride;
  dis["gradient_descent_iterations"] = config.dis.gradient_descent_iterations;
  dis["variational_refinement_iterations"] = config.dis.variational_refinement_iterations;
  dis["use_spatial_propagation"] = config.dis.use_spatial_propagation;

  auto sampling = root["sampling"];
  sampling["grid_step_analysis_px"] = config.sampling.grid_step_analysis_px;
  sampling["min_margin_analysis_px"] = config.sampling.min_margin_analysis_px;
  sampling["lost_border_analysis_px"] = config.sampling.lost_border_analysis_px;

  root["scoring"]["percentile"] = config.scoring.percentile;
  auto trigger = root["trigger"];
  trigger["min_frames_since_keyframe"] = config.trigger.min_frames_since_keyframe;
  trigger["main_threshold_analysis_px"] = config.trigger.main_threshold_analysis_px;
  trigger["min_in_bounds_ratio"] = config.trigger.min_in_bounds_ratio;
  trigger["max_frames_since_keyframe"] = config.trigger.max_frames_since_keyframe;

  auto visualization = root["visualization"];
  visualization["motion_plot_color_rgb"] = colorNode(config.visualization.motion_plot_color_rgb);
  visualization["points_plot_color_rgb"] = colorNode(config.visualization.points_plot_color_rgb);
  visualization["threshold_line_color_rgb"] = colorNode(
      config.visualization.threshold_line_color_rgb);
  visualization["trigger_line_color_rgb"] = colorNode(config.visualization.trigger_line_color_rgb);

  root["output"]["image_format"] = toString(config.output.image_format);

  YAML::Emitter emitter;
  emitter << root;
  if (!emitter.good()) {
    throw std::runtime_error("Failed to serialize configuration");
  }
  return std::string{emitter.c_str()} + "\n";
}

std::string toString(DisPreset preset) {
  switch (preset) {
    case DisPreset::ultrafast:
      return "ultrafast";
    case DisPreset::fast:
      return "fast";
    case DisPreset::medium:
      return "medium";
  }
  throw std::logic_error("Unknown DIS preset");
}

std::string toString(ImageFormat format) {
  switch (format) {
    case ImageFormat::jpg:
      return "jpg";
    case ImageFormat::png:
      return "png";
  }
  throw std::logic_error("Unknown image format");
}

}  // namespace frame_extractor
