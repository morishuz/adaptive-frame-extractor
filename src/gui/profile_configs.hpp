#pragma once

#include "project_state.hpp"

#include "frame_extractor/config.hpp"

#include <array>
#include <filesystem>

namespace frame_extractor::gui {

class ProfileConfigs {
 public:
  [[nodiscard]] static ProfileConfigs load(const std::filesystem::path& directory);
  [[nodiscard]] const Config& forSelection(SelectionChoice selection) const;

 private:
  explicit ProfileConfigs(std::array<Config, 3> configs);

  std::array<Config, 3> configs_;
};

}  // namespace frame_extractor::gui
