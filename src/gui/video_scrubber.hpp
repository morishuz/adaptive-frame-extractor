#pragma once

#include "frame_extractor/decoder.hpp"

#include <opencv2/core.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace frame_extractor::gui {

struct ScrubFrame {
  cv::Mat bgr;
  std::int64_t frame_index{};
  double timestamp_seconds{};
};

struct ScrubberSnapshot {
  bool loading{};
  std::optional<VideoInfo> video_info;
  std::optional<ScrubFrame> frame;
  std::string error;
};

using ScrubSourceFactory = std::function<
    std::unique_ptr<FrameSource>(const std::filesystem::path&)>;

class VideoScrubber {
 public:
  VideoScrubber();
  explicit VideoScrubber(ScrubSourceFactory source_factory);
  ~VideoScrubber();
  VideoScrubber(const VideoScrubber&) = delete;
  VideoScrubber& operator=(const VideoScrubber&) = delete;

  void open(
      std::filesystem::path input_video,
      double initial_timestamp_seconds = 0.0);
  void seek(double timestamp_seconds);
  void stepForward();
  void stepBackward();
  [[nodiscard]] ScrubberSnapshot takeSnapshot();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace frame_extractor::gui
