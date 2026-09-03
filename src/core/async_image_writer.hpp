#pragma once

#include "frame_extractor/detail/image_writer.hpp"

#include <opencv2/core/mat.hpp>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace frame_extractor::detail {

struct AsyncImageWriterSettings {
  std::size_t worker_count{2U};
  // Soft limit: one oversized image is admitted when the writer is otherwise idle.
  std::size_t memory_budget_bytes{128U * 1024U * 1024U};
  std::size_t job_limit{6U};
  ImageEncodingSettings encoding;
};

class AsyncImageWriter final {
 public:
  explicit AsyncImageWriter(
      ImageFormat format,
      AsyncImageWriterSettings settings = {});
  ~AsyncImageWriter();

  AsyncImageWriter(const AsyncImageWriter&) = delete;
  AsyncImageWriter& operator=(const AsyncImageWriter&) = delete;

  void enqueue(std::filesystem::path path, const cv::Mat& frame_bgr);
  void finish();

  [[nodiscard]] std::size_t writtenCount() const;
  [[nodiscard]] const ImageEncodingSettings& encodingSettings() const noexcept;

 private:
  struct PendingImage {
    std::filesystem::path path;
    cv::Mat frame_bgr;
    std::size_t memory_bytes{};
  };

  void writeImage(const PendingImage& image) const;
  [[nodiscard]] bool canAccept(std::size_t image_bytes) const;
  void reserveCapacity(std::size_t memory_bytes);
  void releaseCapacity(std::size_t memory_bytes);
  void releaseCapacityLocked(std::size_t memory_bytes);
  void clearPendingLocked();
  void rethrowFailureLocked() const;
  void closeAndJoin();
  void run();

  const ImageFileWriter file_writer_;
  const AsyncImageWriterSettings settings_;
  mutable std::mutex mutex_;
  std::condition_variable work_available_;
  std::condition_variable queue_space_;
  std::deque<PendingImage> pending_;
  std::vector<std::thread> workers_;
  std::exception_ptr failure_;
  std::size_t written_count_{};
  std::size_t in_flight_bytes_{};
  std::size_t in_flight_images_{};
  bool finishing_{};
};

}  // namespace frame_extractor::detail
