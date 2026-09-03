#include "profile_configs.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace frame_extractor::gui {
namespace {

Config loadProfile(const std::filesystem::path& directory, const char* name) {
  const auto path = directory / (std::string{name} + ".yaml");
  try {
    return loadConfig(path);
  } catch (const std::exception& error) {
    throw std::runtime_error(
        "Cannot load bundled " + std::string{name} + " profile from "
        + path.string() + ": " + error.what());
  }
}

}  // namespace

ProfileConfigs::ProfileConfigs(std::array<Config, 3> configs)
    : configs_{std::move(configs)} {}

ProfileConfigs ProfileConfigs::load(const std::filesystem::path& directory) {
  return ProfileConfigs{{
      loadProfile(directory, "low"),
      loadProfile(directory, "medium"),
      loadProfile(directory, "high")}};
}

const Config& ProfileConfigs::forSelection(SelectionChoice selection) const {
  switch (selection) {
    case SelectionChoice::low:
      return configs_[0];
    case SelectionChoice::high:
      return configs_[2];
    case SelectionChoice::medium:
    case SelectionChoice::fixed:
      return configs_[1];
  }
  throw std::invalid_argument{"Unknown extraction profile"};
}

}  // namespace frame_extractor::gui
