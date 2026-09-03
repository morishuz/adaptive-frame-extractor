#pragma once

#include "model.hpp"
#include "texture.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace frame_extractor::gui {

class KeyframeStrip final {
 public:
  [[nodiscard]] bool append(
      SDL_Renderer* renderer,
      const PendingThumbnail& pending);
  void clear();

  [[nodiscard]] bool empty() const noexcept { return thumbnails_.empty(); }
  [[nodiscard]] std::size_t size() const noexcept { return thumbnails_.size(); }

  void draw(
      std::optional<std::size_t> highlighted_keyframe,
      bool highlight_active,
      bool scroll_to_latest);

 private:
  struct Thumbnail {
    Texture texture;
    std::size_t keyframe_index{};
    std::int64_t decoded_frame_index{};
  };

  std::deque<Thumbnail> thumbnails_;
};

}  // namespace frame_extractor::gui
