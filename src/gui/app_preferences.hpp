#pragma once

#include <filesystem>

namespace frame_extractor::gui {

struct AppPreferences {
  std::filesystem::path last_input_directory;
  std::filesystem::path output_directory;
};

class AppPreferencesStore {
 public:
  explicit AppPreferencesStore(std::filesystem::path path = {});

  [[nodiscard]] AppPreferences load() const;
  void save(const AppPreferences& preferences) const;

 private:
  std::filesystem::path path_;
};

}  // namespace frame_extractor::gui
