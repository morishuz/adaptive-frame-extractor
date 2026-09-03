#include "async_image_writer.hpp"

#include <stdexcept>
#include <utility>

namespace frame_extractor::detail {

AsyncImageWriter::AsyncImageWriter(
    ImageFormat format,
    AsyncImageWriterSettings settings)
    : file_writer_{format, settings.encoding}, settings_{settings} {
  if (settings_.worker_count == 0U || settings_.job_limit == 0U
      || settings_.memory_budget_bytes == 0U) {
    throw std::invalid_argument("image writer limits must be positive");
  }

  workers_.reserve(settings_.worker_count);
  try {
    for (std::size_t index = 0; index < settings_.worker_count; ++index) {
      workers_.emplace_back([this] { run(); });
    }
  } catch (...) {
    closeAndJoin();
    throw;
  }
}

AsyncImageWriter::~AsyncImageWriter() { closeAndJoin(); }

void AsyncImageWriter::enqueue(
    std::filesystem::path path,
    const cv::Mat& frame_bgr) {
  const auto memory_bytes = frame_bgr.total() * frame_bgr.elemSize();
  reserveCapacity(memory_bytes);

  try {
    auto owned_frame = frame_bgr.clone();
    {
      std::lock_guard lock{mutex_};
      rethrowFailureLocked();
      if (finishing_) {
        throw std::logic_error("cannot queue an image after output finalization");
      }
      pending_.push_back(PendingImage{
          std::move(path), std::move(owned_frame), memory_bytes});
    }
  } catch (...) {
    releaseCapacity(memory_bytes);
    throw;
  }
  work_available_.notify_one();
}

void AsyncImageWriter::finish() {
  closeAndJoin();
  std::lock_guard lock{mutex_};
  rethrowFailureLocked();
}

std::size_t AsyncImageWriter::writtenCount() const {
  std::lock_guard lock{mutex_};
  return written_count_;
}

const ImageEncodingSettings& AsyncImageWriter::encodingSettings() const noexcept {
  return settings_.encoding;
}

void AsyncImageWriter::writeImage(const PendingImage& image) const {
  file_writer_.writeAtomically(image.path, image.frame_bgr);
}

bool AsyncImageWriter::canAccept(std::size_t image_bytes) const {
  if (in_flight_images_ >= settings_.job_limit) {
    return false;
  }
  if (in_flight_bytes_ == 0U) {
    return true;
  }
  return image_bytes <= settings_.memory_budget_bytes
      && in_flight_bytes_ <= settings_.memory_budget_bytes - image_bytes;
}

void AsyncImageWriter::reserveCapacity(std::size_t memory_bytes) {
  std::unique_lock lock{mutex_};
  queue_space_.wait(lock, [this, memory_bytes] {
    return canAccept(memory_bytes) || finishing_ || failure_;
  });
  rethrowFailureLocked();
  if (finishing_) {
    throw std::logic_error("cannot queue an image after output finalization");
  }
  in_flight_bytes_ += memory_bytes;
  ++in_flight_images_;
}

void AsyncImageWriter::releaseCapacity(std::size_t memory_bytes) {
  {
    std::lock_guard lock{mutex_};
    releaseCapacityLocked(memory_bytes);
  }
  queue_space_.notify_all();
}

void AsyncImageWriter::releaseCapacityLocked(std::size_t memory_bytes) {
  in_flight_bytes_ -= memory_bytes;
  --in_flight_images_;
}

void AsyncImageWriter::clearPendingLocked() {
  for (const auto& pending : pending_) {
    releaseCapacityLocked(pending.memory_bytes);
  }
  pending_.clear();
}

void AsyncImageWriter::rethrowFailureLocked() const {
  if (failure_) {
    std::rethrow_exception(failure_);
  }
}

void AsyncImageWriter::closeAndJoin() {
  {
    std::lock_guard lock{mutex_};
    finishing_ = true;
  }
  work_available_.notify_all();
  queue_space_.notify_all();
  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void AsyncImageWriter::run() {
  while (true) {
    PendingImage image;
    {
      std::unique_lock lock{mutex_};
      work_available_.wait(lock, [this] {
        return !pending_.empty() || finishing_;
      });
      if (pending_.empty()) {
        return;
      }
      image = std::move(pending_.front());
      pending_.pop_front();
    }

    try {
      writeImage(image);
      {
        std::lock_guard lock{mutex_};
        ++written_count_;
        releaseCapacityLocked(image.memory_bytes);
      }
      queue_space_.notify_all();
    } catch (...) {
      {
        std::lock_guard lock{mutex_};
        if (!failure_) {
          failure_ = std::current_exception();
        }
        releaseCapacityLocked(image.memory_bytes);
        clearPendingLocked();
        finishing_ = true;
      }
      work_available_.notify_all();
      queue_space_.notify_all();
      return;
    }
  }
}

}  // namespace frame_extractor::detail
