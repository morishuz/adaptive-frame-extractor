#pragma once

#include "frame_extractor/config.hpp"
#include "frame_extractor/decoder.hpp"
#include "frame_extractor/processor.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <string_view>

namespace frame_extractor {

namespace detail {
class AsyncImageWriter;
}

inline constexpr int keyframesCsvSchemaVersion = 5;

struct RunPaths {
  std::filesystem::path run_dir;
  std::filesystem::path keyframe_dir;
  std::filesystem::path config_path;
  std::filesystem::path manifest_path;
  std::filesystem::path summary_path;
};

[[nodiscard]] RunPaths createRunPaths(const std::filesystem::path& base_output_dir);

struct RunOutputOptions {
  bool group_keyframes_by_region{};
};

class RunOutputWriter final : public SelectedFrameSink {
 public:
  RunOutputWriter(
      const std::filesystem::path& base_output_dir,
      const Config& config,
      RunOutputOptions options = {});
  ~RunOutputWriter() override;

  void onFrameSelected(const SelectedFrame& selected, const cv::Mat& frame_bgr) override;

  // Drains queued encodes, writes the manifest and summary, and returns the
  // processing runtime with the remaining writer-drain time included. This is
  // a one-shot operation.
  std::chrono::duration<double> finalize(
      const std::filesystem::path& input_video,
      const ProcessOptions& process_options,
      const VideoInfo& video_info,
      const ProcessingResult& result,
      std::chrono::duration<double> processing_runtime,
      std::string_view failure_message = {});

  void discardEmpty();

  [[nodiscard]] const RunPaths& paths() const noexcept { return paths_; }

 private:
  ImageFormat image_format_;
  RunOutputOptions options_;
  RunPaths paths_;
  std::unique_ptr<detail::AsyncImageWriter> image_writer_;
  std::size_t enqueued_images_{};
  bool finalized_{};
};

}  // namespace frame_extractor
