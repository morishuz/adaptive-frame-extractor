#include "keyframe_strip.hpp"

#include "imgui.h"

#include <utility>

namespace frame_extractor::gui {

bool KeyframeStrip::append(
    SDL_Renderer* renderer,
    const PendingThumbnail& pending) {
  Texture texture;
  if (!texture.update(renderer, pending.bgr)) {
    return false;
  }
  if (thumbnails_.size() == thumbnail_history_capacity) {
    thumbnails_.pop_front();
  }
  thumbnails_.push_back(Thumbnail{
      std::move(texture),
      pending.keyframe_index,
      pending.decoded_frame_index});
  return true;
}

void KeyframeStrip::clear() { thumbnails_.clear(); }

void KeyframeStrip::draw(
    std::optional<std::size_t> highlighted_keyframe,
    bool highlight_active,
    bool scroll_to_latest) {
  ImGui::BeginChild(
      "Keyframes",
      ImVec2{0.0F, 0.0F},
      ImGuiChildFlags_Borders,
      ImGuiWindowFlags_HorizontalScrollbar);
  if (thumbnails_.empty()) {
    ImGui::TextDisabled("Selected keyframes will appear here as they are written to disk.");
  }
  for (std::size_t index = 0; index < thumbnails_.size(); ++index) {
    auto& thumbnail = thumbnails_[index];
    ImGui::PushID(static_cast<int>(index));
    ImGui::BeginGroup();
    const ImVec2 size = fitSize(
        thumbnail.texture.width(),
        thumbnail.texture.height(),
        ImVec2{155.0F, 98.0F});
    const ImVec2 image_position = ImGui::GetCursorScreenPos();
    ImGui::Image(thumbnail.texture.reference(), size);
    if (highlight_active && highlighted_keyframe == thumbnail.keyframe_index) {
      ImGui::GetWindowDrawList()->AddRect(
          image_position,
          ImVec2{image_position.x + size.x, image_position.y + size.y},
          IM_COL32(255, 45, 45, 255),
          0.0F,
          0,
          3.0F);
    }
    ImGui::Text(
        "%zu/%lld",
        thumbnail.keyframe_index,
        static_cast<long long>(thumbnail.decoded_frame_index));
    ImGui::EndGroup();
    if (scroll_to_latest && index + 1U == thumbnails_.size()) {
      ImGui::SetScrollHereX(1.0F);
    }
    ImGui::PopID();
    if (index + 1U < thumbnails_.size()) {
      ImGui::SameLine(0.0F, 12.0F);
    }
  }
  ImGui::EndChild();
}

}  // namespace frame_extractor::gui
