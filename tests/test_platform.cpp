#include "platform.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace gui = frame_extractor::gui;

TEST_CASE("GUI filesystem boundaries preserve UTF-8 paths") {
  const std::string encoded = "Caf\xC3\xA9/\xE5\xBD\xB1\xE7\x89\x87.mov";
  const auto path = gui::pathFromUtf8(encoded);

  CHECK(gui::pathToUtf8(path) == encoded);
  CHECK(gui::pathToUtf8(path.filename()) == "\xE5\xBD\xB1\xE7\x89\x87.mov");
  CHECK(gui::pathFromUtf8({}).empty());
  CHECK(gui::pathToUtf8({}).empty());
}
