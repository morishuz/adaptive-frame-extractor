#pragma once

#include "frame_extractor/config.hpp"
#include "frame_extractor/decoder.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace frame_extractor::gui {

enum class ManualFrameExportPhase { idle, saving, complete, failed };

struct ManualFrameExportSnapshot {
  ManualFrameExportPhase phase{ManualFrameExportPhase::idle};
  std::filesystem::path output_path;
  std::optional<std::int64_t> decoded_frame_index;
  std::optional<double> timestamp_seconds;
  bool already_exists{};
  std::string error;
};

using ManualFrameSourceFactory = std::function<
    std::unique_ptr<FrameSource>(const std::filesystem::path&)>;

class ManualFrameExporter final {
 public:
  ManualFrameExporter();
  explicit ManualFrameExporter(ManualFrameSourceFactory source_factory);
  ~ManualFrameExporter();

  ManualFrameExporter(const ManualFrameExporter&) = delete;
  ManualFrameExporter& operator=(const ManualFrameExporter&) = delete;

  bool start(
      std::filesystem::path input_video,
      std::filesystem::path output_directory,
      double timestamp_seconds,
      ImageFormat image_format);
  void reset();
  [[nodiscard]] ManualFrameExportSnapshot takeSnapshot();
  void join();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace frame_extractor::gui
