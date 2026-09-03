#include "smoke_test.hpp"

#include "frame_extractor/decoder.hpp"
#include "texture.hpp"

#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include <SDL3/SDL.h>

#include <exception>
#include <iostream>
#include <stdexcept>

namespace frame_extractor::gui {

int runSmokeTest(SDL_Renderer* renderer, const std::filesystem::path& video) {
  try {
    VideoDecoder decoder{video};
    const auto frame = decoder.read();
    if (!frame || frame->bgr.empty()) {
      throw std::runtime_error{"Smoke-test video has no decodable frame"};
    }
    Texture preview;
    if (!preview.update(renderer, frame->fullBgr())) {
      throw std::runtime_error{SDL_GetError()};
    }
    // Multiple frames exercise ImGui's dynamic font-atlas upload as well as video textures.
    for (int iteration = 0; iteration < 3; ++iteration) {
      SDL_Event event;
      while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL3_ProcessEvent(&event);
      }
      ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();
      ImGui::SetNextWindowPos({0.0F, 0.0F});
      ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
      ImGui::Begin("Frame Extractor startup test", nullptr, ImGuiWindowFlags_NoSavedSettings);
      ImGui::TextUnformatted("Inter font and decoded video preview");
      ImGui::Image(preview.reference(), fitSize(preview.width(), preview.height(),
                                              ImGui::GetContentRegionAvail()));
      ImGui::End();
      ImGui::Render();
      const auto scale = ImGui::GetIO().DisplayFramebufferScale;
      SDL_SetRenderScale(renderer, scale.x, scale.y);
      SDL_SetRenderDrawColor(renderer, 18, 21, 26, 255);
      if (!SDL_RenderClear(renderer)) {
        throw std::runtime_error{SDL_GetError()};
      }
      ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
      SDL_Surface* pixels = SDL_RenderReadPixels(renderer, nullptr);
      if (pixels == nullptr) {
        throw std::runtime_error{SDL_GetError()};
      }
      SDL_DestroySurface(pixels);
      if (!SDL_RenderPresent(renderer)) {
        throw std::runtime_error{SDL_GetError()};
      }
    }
    std::cout << "GUI smoke test passed: font, video decode, texture, render and readback\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "GUI smoke test failed: " << error.what() << '\n';
    return 1;
  }
}

}  // namespace frame_extractor::gui
