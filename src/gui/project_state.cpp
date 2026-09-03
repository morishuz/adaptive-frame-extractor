#include "project_state.hpp"

#include "platform.hpp"

#include "frame_extractor/detail/atomic_output_file.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace frame_extractor::gui {
namespace {

std::string canonicalKey(const std::filesystem::path& path) {
  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  return pathToUtf8((error ? path : absolute).lexically_normal());
}

std::uint64_t fnv1a(std::string_view value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char raw_character : value) {
    const auto character = static_cast<unsigned char>(raw_character);
    hash ^= character;
    hash *= 1099511628211ULL;
  }
  return hash;
}

SelectionChoice parseSelection(const std::string& value) {
  if (value == "low") {
    return SelectionChoice::low;
  }
  if (value == "high") {
    return SelectionChoice::high;
  }
  if (value == "fixed") {
    return SelectionChoice::fixed;
  }
  return SelectionChoice::medium;
}

bool supportedFrameInterval(int interval) {
  return std::ranges::find(supported_frame_intervals, interval)
      != supported_frame_intervals.end();
}

}  // namespace

ProjectStore::ProjectStore(std::filesystem::path directory)
    : directory_{directory.empty()
              ? applicationDataDirectory() / "projects"
              : std::move(directory)} {}

std::filesystem::path ProjectStore::statePath(
    const std::filesystem::path& input_video) const {
  std::ostringstream filename;
  filename << std::hex << std::setfill('0') << std::setw(16)
           << fnv1a(canonicalKey(input_video)) << ".yaml";
  return directory_ / filename.str();
}

ProjectState ProjectStore::load(const std::filesystem::path& input_video) const {
  const auto path = statePath(input_video);
  if (!std::filesystem::is_regular_file(path)) {
    return {};
  }
  std::ifstream input{path, std::ios::binary};
  if (!input.good()) {
    throw std::runtime_error("Cannot read saved project settings");
  }
  const auto root = YAML::Load(input);
  ProjectState state;
  state.selection = parseSelection(root["selection"].as<std::string>("medium"));
  const int schema_version = root["version"].as<int>(0);
  const int legacy_frame_interval = schema_version < 4
      ? root["fixed_fps"].as<int>(8)
      : 8;
  state.frame_interval = root["frame_interval"].as<int>(
      legacy_frame_interval);
  if (!supportedFrameInterval(state.frame_interval)) {
    state.frame_interval = 8;
  }
  state.image_format = root["image_format"].as<std::string>("jpg") == "png"
      ? ImageFormat::png
      : ImageFormat::jpg;
  state.group_keyframes_by_region =
      root["group_keyframes_by_region"].as<bool>(false);
  state.playhead_seconds = root["playhead_seconds"].as<double>(0.0);
  if (!std::isfinite(state.playhead_seconds) || state.playhead_seconds < 0.0) {
    state.playhead_seconds = 0.0;
  }
  if (const auto regions = root["regions"]; regions && regions.IsSequence()) {
    for (const auto& region : regions) {
      state.regions.push_back(TimeRange{
          region["start_seconds"].as<double>(),
          region["end_seconds"].as<double>()});
    }
    state.regions = normalizeTimeRanges(state.regions);
  }
  return state;
}

ProjectLoadResult ProjectStore::loadOrDefault(
    const std::filesystem::path& input_video) const {
  try {
    return ProjectLoadResult{load(input_video), {}};
  } catch (const std::exception& error) {
    return ProjectLoadResult{
        {},
        "Saved project settings were ignored: " + std::string{error.what()}};
  }
}

void ProjectStore::save(
    const std::filesystem::path& input_video,
    const ProjectState& state) const {
  std::error_code error;
  std::filesystem::create_directories(directory_, error);
  if (error) {
    throw std::runtime_error("Cannot create project state directory: " + error.message());
  }

  YAML::Node root;
  root["version"] = 4;
  root["input_video"] = canonicalKey(input_video);
  root["selection"] = selectionChoiceName(state.selection);
  root["frame_interval"] = state.frame_interval;
  root["image_format"] = toString(state.image_format);
  root["group_keyframes_by_region"] = state.group_keyframes_by_region;
  root["playhead_seconds"] = state.playhead_seconds;
  for (const auto& region : normalizeTimeRanges(state.regions)) {
    YAML::Node item;
    item["start_seconds"] = region.start_seconds;
    item["end_seconds"] = region.end_seconds;
    root["regions"].push_back(item);
  }

  YAML::Emitter emitter;
  emitter << root;
  if (!emitter.good()) {
    throw std::runtime_error("Cannot serialize project state");
  }
  detail::writeTextAtomically(statePath(input_video), emitter.c_str());
}

const char* selectionChoiceName(SelectionChoice selection) {
  switch (selection) {
    case SelectionChoice::low:
      return "low";
    case SelectionChoice::medium:
      return "medium";
    case SelectionChoice::high:
      return "high";
    case SelectionChoice::fixed:
      return "fixed";
  }
  return "medium";
}

ProcessOptions processOptionsForProject(
    const ProjectState& state,
    std::string input_label) {
  ProcessOptions options;
  options.input_label = std::move(input_label);
  options.regions = state.regions;
  options.selection_profile = selectionChoiceName(state.selection);
  if (state.selection == SelectionChoice::fixed) {
    options.fixed_frame_interval = static_cast<std::size_t>(state.frame_interval);
  }
  return options;
}

Config configForProject(const ProjectState& state, Config profile_config) {
  profile_config.output.image_format = state.image_format;
  return profile_config;
}

RunOutputOptions outputOptionsForProject(const ProjectState& state) {
  return RunOutputOptions{
      state.group_keyframes_by_region && !state.regions.empty()};
}

std::optional<std::size_t> regionIndexAt(
    std::span<const TimeRange> regions,
    double timestamp_seconds) {
  constexpr double tolerance = 1.0e-9;
  for (std::size_t index = 0; index < regions.size(); ++index) {
    if (timestamp_seconds >= regions[index].start_seconds - tolerance
        && timestamp_seconds <= regions[index].end_seconds + tolerance) {
      return index;
    }
  }
  return std::nullopt;
}

void addRegion(ProjectState& state, double first_seconds, double second_seconds) {
  state.regions.push_back(TimeRange{
      std::min(first_seconds, second_seconds),
      std::max(first_seconds, second_seconds)});
  state.regions = normalizeTimeRanges(state.regions);
}

bool removeRegionAt(ProjectState& state, double timestamp_seconds) {
  const auto index = regionIndexAt(state.regions, timestamp_seconds);
  if (!index) {
    return false;
  }
  state.regions.erase(state.regions.begin() + static_cast<std::ptrdiff_t>(*index));
  return true;
}

}  // namespace frame_extractor::gui
