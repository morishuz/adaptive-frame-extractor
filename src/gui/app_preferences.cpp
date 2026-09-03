#include "app_preferences.hpp"

#include "platform.hpp"

#include "frame_extractor/detail/atomic_output_file.hpp"

#include <yaml-cpp/yaml.h>

#include <fstream>
#include <stdexcept>
#include <utility>

namespace frame_extractor::gui {

AppPreferencesStore::AppPreferencesStore(std::filesystem::path path)
    : path_{path.empty()
              ? applicationDataDirectory() / "preferences.yaml"
              : std::move(path)} {}

AppPreferences AppPreferencesStore::load() const {
  if (!std::filesystem::is_regular_file(path_)) {
    return {};
  }
  std::ifstream input{path_, std::ios::binary};
  if (!input.good()) {
    throw std::runtime_error("Cannot read application preferences");
  }
  const auto root = YAML::Load(input);
  return AppPreferences{
      pathFromUtf8(root["last_input_directory"].as<std::string>("")),
      pathFromUtf8(root["output_directory"].as<std::string>(""))};
}

void AppPreferencesStore::save(const AppPreferences& preferences) const {
  std::error_code error;
  const auto parent = path_.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, error);
    if (error) {
      throw std::runtime_error(
          "Cannot create preferences directory: " + error.message());
    }
  }

  YAML::Node root;
  root["version"] = 1;
  root["last_input_directory"] = pathToUtf8(preferences.last_input_directory);
  root["output_directory"] = pathToUtf8(preferences.output_directory);

  YAML::Emitter emitter;
  emitter << root;
  if (!emitter.good()) {
    throw std::runtime_error("Cannot serialize application preferences");
  }
  detail::writeTextAtomically(path_, emitter.c_str());
}

}  // namespace frame_extractor::gui
