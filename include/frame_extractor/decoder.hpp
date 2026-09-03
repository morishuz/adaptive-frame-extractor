#pragma once

#include <opencv2/core.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace frame_extractor {

struct Rational {
  int numerator{};
  int denominator{1};

  [[nodiscard]] double toSeconds(std::int64_t value) const;
  bool operator==(const Rational&) const = default;
};

struct VideoInfo {
  int width{};
  int height{};
  Rational time_base{};
  Rational average_frame_rate{};
  std::optional<std::int64_t> reported_frame_count{};
  std::string codec_name{};
  int rotation_degrees{};
  std::optional<double> duration_seconds{};
  std::optional<double> start_time_seconds{};
  bool exact_frame_indices_after_seek{};
  bool hardware_accelerated_decode{};

  [[nodiscard]] std::optional<double> framesPerSecond() const;
  [[nodiscard]] std::optional<double> estimatedDurationSeconds() const;
};

enum class AnalysisConversionMethod {
  none,
  opencv_native_luma,
  ffmpeg_fallback,
};

struct FrameDecodeTimings {
  double packet_decode_seconds{};
  double hardware_transfer_seconds{};
  double pixel_conversion_seconds{};
  double rotation_seconds{};
  AnalysisConversionMethod analysis_conversion_method{
      AnalysisConversionMethod::none};
};

struct DecodedFrame {
  mutable cv::Mat bgr{};
  std::int64_t decoded_frame_index{};
  std::optional<std::int64_t> pts{};
  Rational time_base{};
  FrameDecodeTimings timings{};
  cv::Mat analysis_gray{};
  std::function<cv::Mat()> render_bgr{};
  std::function<cv::Mat(int, int)> render_preview_bgr{};

  [[nodiscard]] std::optional<double> ptsSeconds() const;
  [[nodiscard]] const cv::Mat& fullBgr() const;
  [[nodiscard]] cv::Mat previewBgr(int maximum_width, int maximum_height) const;
};

[[nodiscard]] double relativeTimestampSeconds(
    const DecodedFrame& frame,
    const VideoInfo& info);

class FrameSource {
 public:
  virtual ~FrameSource() = default;
  [[nodiscard]] virtual const VideoInfo& info() const = 0;
  [[nodiscard]] virtual std::optional<DecodedFrame> read() = 0;
  virtual bool seekToSeconds(double seconds) {
    (void)seconds;
    return false;
  }
};

struct VideoDecoderOptions {
  std::optional<int> target_analysis_area_px{};
  bool defer_full_bgr{};
};

class VideoDecoder final : public FrameSource {
 public:
  explicit VideoDecoder(
      const std::filesystem::path& path,
      VideoDecoderOptions options = {});
  ~VideoDecoder();

  VideoDecoder(const VideoDecoder&) = delete;
  VideoDecoder& operator=(const VideoDecoder&) = delete;
  VideoDecoder(VideoDecoder&&) noexcept;
  VideoDecoder& operator=(VideoDecoder&&) noexcept;

  [[nodiscard]] const VideoInfo& info() const override;
  [[nodiscard]] std::optional<DecodedFrame> read() override;
  bool seekToSeconds(double seconds) override;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace frame_extractor
