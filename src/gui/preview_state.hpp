#pragma once

#include "model.hpp"
#include "texture.hpp"
#include "video_scrubber.hpp"

#include "frame_extractor/tracking.hpp"

#include <cstdint>
#include <vector>

namespace frame_extractor::gui {

struct PreviewState {
  Texture texture;
  std::int64_t frame_index{-1};
  double timestamp_seconds{};
  int frames_since_keyframe{};
  std::vector<Point2f> tracking_points;

  void clear();
  void prepareForRun();
  bool update(SDL_Renderer* renderer, const ScrubFrame& frame);
  bool update(SDL_Renderer* renderer, PendingImage&& frame);
};

}  // namespace frame_extractor::gui
