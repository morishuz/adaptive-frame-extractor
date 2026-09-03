#include "preview_state.hpp"

#include <utility>

namespace frame_extractor::gui {

void PreviewState::clear() {
  texture.reset();
  frame_index = -1;
  timestamp_seconds = 0.0;
  frames_since_keyframe = 0;
  tracking_points.clear();
}

void PreviewState::prepareForRun() {
  frames_since_keyframe = 0;
  tracking_points.clear();
}

bool PreviewState::update(SDL_Renderer* renderer, const ScrubFrame& frame) {
  if (!texture.update(renderer, frame.bgr)) {
    return false;
  }
  frame_index = frame.frame_index;
  timestamp_seconds = frame.timestamp_seconds;
  frames_since_keyframe = 0;
  tracking_points.clear();
  return true;
}

bool PreviewState::update(SDL_Renderer* renderer, PendingImage&& frame) {
  if (!texture.update(renderer, frame.bgr)) {
    return false;
  }
  frame_index = frame.decoded_frame_index;
  timestamp_seconds = frame.timestamp_seconds;
  frames_since_keyframe = frame.frames_since_keyframe;
  tracking_points = std::move(frame.normalized_tracking_points);
  return true;
}

}  // namespace frame_extractor::gui
