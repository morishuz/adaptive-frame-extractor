#include "manual_frame_exporter.hpp"
#include "platform.hpp"

#include "frame_extractor/detail/image_writer.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

namespace frame_extractor::gui {
namespace {

constexpr double timestamp_tolerance_seconds = 1.0e-9;
constexpr std::string_view invalid_filename_characters{"<>:\"/\\|?*"};

ManualFrameSourceFactory requireSourceFactory(
    ManualFrameSourceFactory source_factory) {
  if (!source_factory) {
    throw std::invalid_argument("Manual frame source factory must be callable");
  }
  return source_factory;
}

bool isWindowsDeviceName(std::string_view name) {
  const auto extension = name.find('.');
  std::string base{name.substr(0U, extension)};
  for (char& character : base) {
    if (character >= 'a' && character <= 'z') {
      character = static_cast<char>(character - 'a' + 'A');
    }
  }
  if (base == "CON" || base == "PRN" || base == "AUX" || base == "NUL") {
    return true;
  }
  return base.size() == 4U
      && (base.starts_with("COM") || base.starts_with("LPT"))
      && base.back() >= '1' && base.back() <= '9';
}

std::string sanitizedVideoStem(const std::filesystem::path& input_video) {
  std::string result = pathToUtf8(input_video.stem());
  std::ranges::replace_if(result, [](char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte < 32U || byte == 127U
        || invalid_filename_characters.find(character) != std::string_view::npos;
  }, '_');
  while (!result.empty() && (result.back() == ' ' || result.back() == '.')) {
    result.pop_back();
  }
  if (result.empty() || result == "." || result == "..") {
    return "video";
  }
  if (isWindowsDeviceName(result)) {
    result.insert(result.begin(), '_');
  }
  return result;
}

std::string canonicalSourceKey(const std::filesystem::path& input_video) {
  std::error_code error;
  auto canonical = std::filesystem::weakly_canonical(input_video, error);
  if (error) {
    error.clear();
    canonical = std::filesystem::absolute(input_video, error);
  }
  return pathToUtf8((error ? input_video : canonical).lexically_normal());
}

std::uint64_t fnv1a(std::string_view value) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char raw_character : value) {
    hash ^= static_cast<unsigned char>(raw_character);
    hash *= 1099511628211ULL;
  }
  return hash;
}

std::filesystem::path sourceDirectoryName(const std::filesystem::path& input_video) {
  std::ostringstream name;
  name << sanitizedVideoStem(input_video) << '-' << std::hex << std::setfill('0')
       << std::setw(16) << fnv1a(canonicalSourceKey(input_video));
  return pathFromUtf8(name.str());
}

std::filesystem::path outputPath(
    const std::filesystem::path& directory,
    std::int64_t decoded_frame_index,
    ImageFormat image_format) {
  std::ostringstream filename;
  filename << "frame_" << std::setfill('0') << std::setw(6)
           << std::max<std::int64_t>(0, decoded_frame_index) << '.'
           << toString(image_format);
  return directory / filename.str();
}

bool existingRegularFile(const std::filesystem::path& path) {
  std::error_code error;
  const bool exists = std::filesystem::exists(path, error);
  if (error) {
    throw std::runtime_error(
        "Cannot inspect manual frame output: " + error.message());
  }
  if (!exists) {
    return false;
  }
  if (!std::filesystem::is_regular_file(path, error)) {
    if (error) {
      throw std::runtime_error(
          "Cannot inspect manual frame output: " + error.message());
    }
    throw std::runtime_error(
        "Manual frame output path is not a file: " + path.string());
  }
  return true;
}

}  // namespace

class ManualFrameExporter::Impl {
 public:
  explicit Impl(ManualFrameSourceFactory source_factory)
      : source_factory_{requireSourceFactory(std::move(source_factory))} {}

  ~Impl() { join(); }

  bool start(
      std::filesystem::path input_video,
      std::filesystem::path output_directory,
      double timestamp_seconds,
      ImageFormat image_format) {
    reapFinished();
    if (worker_.joinable()) {
      return false;
    }

    if (input_video.empty() || output_directory.empty()
        || !std::isfinite(timestamp_seconds) || timestamp_seconds < 0.0) {
      std::lock_guard lock{mutex_};
      snapshot_ = ManualFrameExportSnapshot{
          .phase = ManualFrameExportPhase::failed,
          .error = "Manual frame export requires a video, output folder, and valid time."};
      return false;
    }

    {
      std::lock_guard lock{mutex_};
      snapshot_ = ManualFrameExportSnapshot{
          .phase = ManualFrameExportPhase::saving};
    }
    worker_done_.store(false, std::memory_order_relaxed);

    try {
      worker_ = std::thread{
          [this,
           input_video = std::move(input_video),
           output_directory = std::move(output_directory),
           timestamp_seconds,
           image_format] {
            run(
                input_video,
                output_directory,
                timestamp_seconds,
                image_format);
          }};
    } catch (const std::exception& error) {
      std::lock_guard lock{mutex_};
      snapshot_.phase = ManualFrameExportPhase::failed;
      snapshot_.error = error.what();
      worker_done_.store(true, std::memory_order_release);
      return false;
    }
    return true;
  }

  void reset() {
    reapFinished();
    if (worker_.joinable()) {
      return;
    }
    std::lock_guard lock{mutex_};
    snapshot_ = {};
  }

  ManualFrameExportSnapshot takeSnapshot() {
    reapFinished();
    std::lock_guard lock{mutex_};
    return snapshot_;
  }

  void join() {
    if (worker_.joinable()) {
      worker_.join();
    }
  }

 private:
  void run(
      const std::filesystem::path& input_video,
      const std::filesystem::path& output_directory,
      double target_seconds,
      ImageFormat image_format) noexcept {
    try {
      const auto frame_directory = output_directory / "manual_frames"
          / sourceDirectoryName(input_video);
      auto source = source_factory_(input_video);
      if (!source) {
        throw std::runtime_error("Manual frame source factory returned no source");
      }
      if (!source->seekToSeconds(target_seconds) && target_seconds > 0.0) {
        throw std::runtime_error("Video source does not support seeking");
      }

      std::optional<DecodedFrame> selected;
      double selected_timestamp{};
      while (auto frame = source->read()) {
        const double timestamp = relativeTimestampSeconds(*frame, source->info());
        if (timestamp + timestamp_tolerance_seconds >= target_seconds) {
          selected_timestamp = timestamp;
          selected = std::move(*frame);
          break;
        }
      }
      if (!selected) {
        throw std::runtime_error("No video frame exists at or after the selected time");
      }

      const auto output_path = outputPath(
          frame_directory, selected->decoded_frame_index, image_format);
      const bool already_exists = existingRegularFile(output_path);

      if (!already_exists) {
        detail::ImageFileWriter{image_format}.writeAtomically(
            output_path, selected->fullBgr());
      }

      std::lock_guard lock{mutex_};
      snapshot_ = ManualFrameExportSnapshot{
          .phase = ManualFrameExportPhase::complete,
          .output_path = output_path,
          .decoded_frame_index = selected->decoded_frame_index,
          .timestamp_seconds = selected_timestamp,
          .already_exists = already_exists};
    } catch (const std::exception& error) {
      std::lock_guard lock{mutex_};
      snapshot_ = ManualFrameExportSnapshot{
          .phase = ManualFrameExportPhase::failed,
          .error = error.what()};
    } catch (...) {
      std::lock_guard lock{mutex_};
      snapshot_ = ManualFrameExportSnapshot{
          .phase = ManualFrameExportPhase::failed,
          .error = "Manual frame export failed"};
    }
    worker_done_.store(true, std::memory_order_release);
  }

  void reapFinished() {
    if (worker_.joinable() && worker_done_.load(std::memory_order_acquire)) {
      worker_.join();
    }
  }

  ManualFrameSourceFactory source_factory_;
  std::mutex mutex_;
  ManualFrameExportSnapshot snapshot_;
  std::thread worker_;
  std::atomic_bool worker_done_{false};
};

ManualFrameExporter::ManualFrameExporter()
    : ManualFrameExporter{[](const std::filesystem::path& input_video) {
        return std::make_unique<VideoDecoder>(
            input_video,
            VideoDecoderOptions{.defer_full_bgr = true});
      }} {}

ManualFrameExporter::ManualFrameExporter(
    ManualFrameSourceFactory source_factory)
    : impl_{std::make_unique<Impl>(std::move(source_factory))} {}

ManualFrameExporter::~ManualFrameExporter() = default;

bool ManualFrameExporter::start(
    std::filesystem::path input_video,
    std::filesystem::path output_directory,
    double timestamp_seconds,
    ImageFormat image_format) {
  return impl_->start(
      std::move(input_video),
      std::move(output_directory),
      timestamp_seconds,
      image_format);
}

void ManualFrameExporter::reset() { impl_->reset(); }

ManualFrameExportSnapshot ManualFrameExporter::takeSnapshot() {
  return impl_->takeSnapshot();
}

void ManualFrameExporter::join() { impl_->join(); }

}  // namespace frame_extractor::gui
