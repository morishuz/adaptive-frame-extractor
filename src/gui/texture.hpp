#pragma once

#include "imgui.h"

#include <SDL3/SDL.h>

#include <opencv2/core.hpp>

namespace frame_extractor::gui {

class Texture {
 public:
  Texture() = default;
  ~Texture();
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  Texture(Texture&& other) noexcept;
  Texture& operator=(Texture&& other) noexcept;

  bool update(SDL_Renderer* renderer, const cv::Mat& bgr);
  void reset();
  [[nodiscard]] bool valid() const;
  [[nodiscard]] int width() const;
  [[nodiscard]] int height() const;
  [[nodiscard]] ImTextureRef reference() const;

 private:
  SDL_Texture* texture_{};
  int width_{};
  int height_{};
};

[[nodiscard]] ImVec2 fitSize(int width, int height, ImVec2 available);

}  // namespace frame_extractor::gui
