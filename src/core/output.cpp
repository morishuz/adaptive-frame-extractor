#include "frame_extractor/output.hpp"

#include "async_image_writer.hpp"
#include "run_files.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace frame_extractor {
namespace {

std::tm localTime(std::time_t value) {
  std::tm result{};
#ifdef _WIN32
  localtime_s(&result, &value);
#else
  localtime_r(&value, &result);
#endif
  return result;
}

}  // namespace

RunPaths createRunPaths(const std::filesystem::path& base_output_dir) {
  if (base_output_dir.empty()) {
    throw std::invalid_argument("output directory must not be empty");
  }
  std::error_code error;
  std::filesystem::create_directories(base_output_dir, error);
  if (error) {
    throw std::runtime_error("Cannot create output directory: " + error.message());
  }

  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  const auto local = localTime(time);
  std::ostringstream stem;
  stem << std::put_time(&local, "%Y%m%d_%H%M%S");

  std::filesystem::path run_dir;
  for (std::size_t suffix = 0;; ++suffix) {
    std::ostringstream name;
    name << stem.str();
    if (suffix > 0U) {
      name << '_' << std::setfill('0') << std::setw(2) << suffix;
    }
    run_dir = base_output_dir / name.str();
    error.clear();
    if (std::filesystem::create_directory(run_dir, error)) {
      break;
    }
    if (error && error != std::errc::file_exists) {
      throw std::runtime_error("Cannot create run directory: " + error.message());
    }
  }

  const auto keyframe_dir = run_dir / "keyframes";
  if (!std::filesystem::create_directory(keyframe_dir, error) || error) {
    std::error_code cleanup_error;
    std::filesystem::remove_all(run_dir, cleanup_error);
    throw std::runtime_error("Cannot create keyframe directory: " + error.message());
  }
  return RunPaths{
      run_dir,
      keyframe_dir,
      run_dir / "config.yaml",
      run_dir / "keyframes.csv",
      run_dir / "summary.txt"};
}

RunOutputWriter::RunOutputWriter(
    const std::filesystem::path& base_output_dir,
    const Config& config,
    RunOutputOptions options)
    : image_format_{config.output.image_format},
      options_{options},
      paths_{createRunPaths(base_output_dir)} {
  try {
    detail::writeConfigSnapshot(paths_, config);
    image_writer_ = std::make_unique<detail::AsyncImageWriter>(image_format_);
  } catch (...) {
    std::error_code error;
    std::filesystem::remove_all(paths_.run_dir, error);
    throw;
  }
}

RunOutputWriter::~RunOutputWriter() = default;

void RunOutputWriter::onFrameSelected(
    const SelectedFrame& selected,
    const cv::Mat& frame_bgr) {
  if (selected.keyframe_index != enqueued_images_) {
    throw std::runtime_error("keyframes must be written in sequential index order");
  }
  if (frame_bgr.empty()) {
    throw std::invalid_argument("selected keyframe image must not be empty");
  }
  const auto image_path = paths_.run_dir / detail::keyframeRelativePath(
      selected, image_format_, options_.group_keyframes_by_region);
  image_writer_->enqueue(image_path, frame_bgr);
  ++enqueued_images_;
}

std::chrono::duration<double> RunOutputWriter::finalize(
    const std::filesystem::path& input_video,
    const ProcessOptions& process_options,
    const VideoInfo& video_info,
    const ProcessingResult& result,
    std::chrono::duration<double> processing_runtime,
    std::string_view failure_message) {
  if (finalized_) {
    throw std::logic_error("run output has already been finalized");
  }
  const auto drain_started = std::chrono::steady_clock::now();
  image_writer_->finish();
  const auto runtime = processing_runtime + std::chrono::duration<double>(
      std::chrono::steady_clock::now() - drain_started);

  if (result.processed_frames == 0U || result.selected_frames.empty()) {
    throw std::invalid_argument("cannot finalize an empty processing result");
  }
  if (image_writer_->writtenCount() != result.selected_frames.size()) {
    throw std::runtime_error("written keyframe count does not match processing result");
  }

  // Report publication is intentionally one-shot. This avoids platform-specific
  // retry behavior when one atomic rename succeeds and a later one fails.
  finalized_ = true;
  detail::writeRunReports(
      paths_,
      input_video,
      process_options,
      video_info,
      result,
      runtime,
      image_format_,
      options_.group_keyframes_by_region,
      image_writer_->encodingSettings(),
      failure_message);
  return runtime;
}

void RunOutputWriter::discardEmpty() {
  if (finalized_) {
    throw std::logic_error("run output has already been finalized");
  }
  image_writer_->finish();
  if (enqueued_images_ != 0U) {
    throw std::logic_error("cannot discard run output after selecting frames");
  }
  std::error_code error;
  std::filesystem::remove_all(paths_.run_dir, error);
  if (error) {
    throw std::runtime_error("Cannot remove empty run directory: " + error.message());
  }
  finalized_ = true;
}

}  // namespace frame_extractor
