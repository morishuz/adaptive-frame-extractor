#pragma once

#include "frame_extractor/output.hpp"
#include "frame_extractor/processor.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace frame_extractor::gui {

enum class SelectionChoice { low, medium, high, fixed };

inline constexpr std::array supported_frame_intervals{4, 6, 8, 10, 12, 16};

struct ProjectState {
  SelectionChoice selection{SelectionChoice::medium};
  int frame_interval{8};
  ImageFormat image_format{ImageFormat::jpg};
  bool group_keyframes_by_region{};
  double playhead_seconds{};
  std::vector<TimeRange> regions;
};

struct ProjectLoadResult {
  ProjectState state;
  std::string warning;
};

class ProjectStore {
 public:
  explicit ProjectStore(std::filesystem::path directory = {});

  [[nodiscard]] ProjectState load(const std::filesystem::path& input_video) const;
  [[nodiscard]] ProjectLoadResult loadOrDefault(
      const std::filesystem::path& input_video) const;
  void save(const std::filesystem::path& input_video, const ProjectState& state) const;

 private:
  [[nodiscard]] std::filesystem::path statePath(
      const std::filesystem::path& input_video) const;

  std::filesystem::path directory_;
};

[[nodiscard]] const char* selectionChoiceName(SelectionChoice selection);
[[nodiscard]] ProcessOptions processOptionsForProject(
    const ProjectState& state,
    std::string input_label);
[[nodiscard]] Config configForProject(const ProjectState& state, Config profile_config);
[[nodiscard]] RunOutputOptions outputOptionsForProject(const ProjectState& state);
[[nodiscard]] std::optional<std::size_t> regionIndexAt(
    std::span<const TimeRange> regions,
    double timestamp_seconds);
void addRegion(ProjectState& state, double first_seconds, double second_seconds);
[[nodiscard]] bool removeRegionAt(ProjectState& state, double timestamp_seconds);

}  // namespace frame_extractor::gui
