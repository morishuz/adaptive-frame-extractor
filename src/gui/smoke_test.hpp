#pragma once

#include <filesystem>

struct SDL_Renderer;

namespace frame_extractor::gui {

// Uses the application's initialized SDL/ImGui backends; never reads or saves user state.
int runSmokeTest(SDL_Renderer* renderer, const std::filesystem::path& video);

}  // namespace frame_extractor::gui
