#pragma once

#include "frame_extractor/tracking.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace frame_extractor {

struct RunStartedEvent {
  std::string input_path;
  std::optional<std::size_t> total_frames;
};

struct FrameAnalyzedEvent {
  std::size_t processed_index{};
  FrameScores scores{};
  TriggerDecision trigger{};
  double decode_seconds{};
  double flow_seconds{};
  std::size_t region_index{};
};

struct KeyframeSelectedEvent {
  std::size_t keyframe_index{};
  std::size_t processed_index{};
  std::int64_t decoded_frame_index{};
  std::string selection_reason;
};

struct KeyframeUpdatedEvent {
  std::size_t keyframe_index{};
  std::string selection_reason;
};

struct WarningEvent {
  std::string message;
};

struct RunFinishedEvent {
  std::size_t processed_frames{};
  std::size_t selected_keyframes{};
  bool cancelled{};
};

using DiagnosticEvent = std::variant<
    RunStartedEvent,
    FrameAnalyzedEvent,
    KeyframeSelectedEvent,
    KeyframeUpdatedEvent,
    WarningEvent,
    RunFinishedEvent>;

class DiagnosticObserver {
 public:
  virtual ~DiagnosticObserver() = default;
  virtual void onEvent(const DiagnosticEvent& event) = 0;
};

class CancellationToken {
 public:
  static_assert(std::atomic_bool::is_always_lock_free);
  void requestCancellation() noexcept { cancelled_.store(true, std::memory_order_relaxed); }
  [[nodiscard]] bool isCancellationRequested() const noexcept {
    return cancelled_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic_bool cancelled_{false};
};

}  // namespace frame_extractor
