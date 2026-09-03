#include "app_preferences.hpp"
#include "platform.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>

namespace gui = frame_extractor::gui;

namespace {

std::filesystem::path uniqueTemporaryDirectory() {
  return std::filesystem::temp_directory_path()
      / ("frame_extractor_preferences_"
         + std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
}

}  // namespace

TEST_CASE("application preferences round trip dialog locations") {
  const auto directory = uniqueTemporaryDirectory();
  const gui::AppPreferencesStore store{directory / "preferences.yaml"};
  const gui::AppPreferences saved{
      directory / "input videos",
      directory / "output"};

  store.save(saved);
  auto updated = saved;
  updated.output_directory = directory / "updated output";
  store.save(updated);
  const auto loaded = store.load();

  CHECK(loaded.last_input_directory == updated.last_input_directory);
  CHECK(loaded.output_directory == updated.output_directory);
  CHECK_FALSE(std::filesystem::exists(directory / "preferences.part.yaml"));
  std::filesystem::remove_all(directory);
}

TEST_CASE("file URLs encode paths without invoking the platform opener") {
  const auto url = gui::pathToFileUrl(
      std::filesystem::temp_directory_path() / "frame extractor" / "summary#1.txt");

  CHECK(url.starts_with("file:///"));
  CHECK(url.find("frame%20extractor") != std::string::npos);
  CHECK(url.find("summary%231.txt") != std::string::npos);
  CHECK(url.find(' ') == std::string::npos);
}
