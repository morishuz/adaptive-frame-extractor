#include "texture.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <utility>

namespace frame_extractor::gui {

Texture::~Texture() { reset(); }

Texture::Texture(Texture&& other) noexcept
    : texture_{std::exchange(other.texture_, nullptr)},
      width_{std::exchange(other.width_, 0)},
      height_{std::exchange(other.height_, 0)} {}

Texture& Texture::operator=(Texture&& other) noexcept {
  if (this != &other) {
    reset();
    texture_ = std::exchange(other.texture_, nullptr);
    width_ = std::exchange(other.width_, 0);
    height_ = std::exchange(other.height_, 0);
  }
  return *this;
}

bool Texture::update(SDL_Renderer* renderer, const cv::Mat& bgr) {
  if (bgr.empty() || bgr.type() != CV_8UC3
      || bgr.step > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  if (texture_ == nullptr || width_ != bgr.cols || height_ != bgr.rows) {
    reset();
    texture_ = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_BGR24, SDL_TEXTUREACCESS_STREAMING, bgr.cols, bgr.rows);
    if (texture_ == nullptr) {
      return false;
    }
    SDL_SetTextureScaleMode(texture_, SDL_SCALEMODE_LINEAR);
    width_ = bgr.cols;
    height_ = bgr.rows;
  }
  return SDL_UpdateTexture(texture_, nullptr, bgr.data, static_cast<int>(bgr.step));
}

void Texture::reset() {
  if (texture_ != nullptr) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  width_ = 0;
  height_ = 0;
}

bool Texture::valid() const { return texture_ != nullptr; }
int Texture::width() const { return width_; }
int Texture::height() const { return height_; }

ImTextureRef Texture::reference() const {
  return ImTextureRef{
      static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture_))};
}

ImVec2 fitSize(int width, int height, ImVec2 available) {
  if (width <= 0 || height <= 0 || available.x <= 0.0F || available.y <= 0.0F) {
    return {};
  }
  const float scale = std::min(
      available.x / static_cast<float>(width),
      available.y / static_cast<float>(height));
  return {static_cast<float>(width) * scale, static_cast<float>(height) * scale};
}

}  // namespace frame_extractor::gui
