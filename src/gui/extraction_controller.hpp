#pragma once

#include "model.hpp"

#include "frame_extractor/config.hpp"
#include "frame_extractor/output.hpp"
#include "frame_extractor/processor.hpp"

#include <filesystem>
#include <memory>

namespace frame_extractor::gui {

class ExtractionController {
 public:
  ExtractionController();
  ~ExtractionController();
  ExtractionController(const ExtractionController&) = delete;
  ExtractionController& operator=(const ExtractionController&) = delete;

  bool start(
      std::filesystem::path input_video,
      std::filesystem::path output_directory,
      Config config,
      ProcessOptions options,
      RunOutputOptions output_options);
  void reset();
  void cancel();
  [[nodiscard]] RunSnapshot takeSnapshot();
  void join();

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace frame_extractor::gui
