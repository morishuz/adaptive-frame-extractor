#include "video_scrubber.hpp"

#include <algorithm>
#include <condition_variable>
#include <cmath>
#include <cstdint>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <variant>

namespace frame_extractor::gui {
namespace {

using SourceGeneration = std::uint64_t;
using NavigationRevision = std::uint64_t;

struct OpenRequest {
  std::filesystem::path input_video;
  double initial_timestamp_seconds{};
  SourceGeneration generation{};
};
struct SeekRequest {
  double timestamp_seconds{};
  SourceGeneration generation{};
  NavigationRevision navigation_revision{};
};
struct StepRequest {
  int delta{};
  SourceGeneration generation{};
  NavigationRevision navigation_revision{};
};
using Request = std::variant<OpenRequest, SeekRequest, StepRequest>;

ScrubSourceFactory requireSourceFactory(ScrubSourceFactory source_factory) {
  if (!source_factory) {
    throw std::invalid_argument("Scrub source factory must be callable");
  }
  return source_factory;
}

}  // namespace

class VideoScrubber::Impl {
 public:
  explicit Impl(ScrubSourceFactory source_factory)
      : source_factory_{requireSourceFactory(std::move(source_factory))},
        worker_{[this] { run(); }} {}

  ~Impl() {
    {
      std::lock_guard lock{mutex_};
      stopping_ = true;
    }
    wake_.notify_one();
    worker_.join();
  }

  void open(std::filesystem::path input_video, double initial_timestamp_seconds) {
    if (!std::isfinite(initial_timestamp_seconds) || initial_timestamp_seconds < 0.0) {
      throw std::invalid_argument(
          "initial scrubber timestamp must be finite and non-negative");
    }
    {
      std::lock_guard lock{mutex_};
      const SourceGeneration generation = ++requested_generation_;
      ++requested_navigation_revision_;
      pending_open_ = OpenRequest{
          std::move(input_video), initial_timestamp_seconds, generation};
      pending_navigation_.clear();
      snapshot_.loading = true;
      snapshot_.video_info.reset();
      snapshot_.frame.reset();
      snapshot_.error.clear();
    }
    wake_.notify_one();
  }

  void seek(double timestamp_seconds) {
    requestNavigation(SeekRequest{.timestamp_seconds = timestamp_seconds});
  }

  void step(int direction) {
    requestNavigation(StepRequest{.delta = direction < 0 ? -1 : 1});
  }

  ScrubberSnapshot takeSnapshot() {
    std::lock_guard lock{mutex_};
    auto frame = std::exchange(snapshot_.frame, {});
    ScrubberSnapshot result = snapshot_;
    result.frame = std::move(frame);
    return result;
  }

 private:
  void run() {
    while (true) {
      std::optional<Request> request;
      {
        std::unique_lock lock{mutex_};
        wake_.wait(lock, [this] {
          return stopping_ || pending_open_.has_value()
              || !pending_navigation_.empty();
        });
        if (stopping_) {
          return;
        }
        if (pending_open_) {
          request = std::exchange(pending_open_, {});
        } else {
          request = std::move(pending_navigation_.front());
          pending_navigation_.pop_front();
        }
      }
      try {
        std::visit([this](const auto& value) { handle(value); }, *request);
      } catch (const std::exception& error) {
        publishError(*request, error.what());
      }
    }
  }

  void handle(const OpenRequest& request) {
    source_.reset();
    active_generation_.reset();
    cache_.clear();
    cache_position_ = 0U;
    auto source = source_factory_(request.input_video);
    if (!source) {
      throw std::runtime_error("Scrub source factory returned no source");
    }
    if (!isCurrent(request)) {
      return;
    }
    source_ = std::move(source);
    active_generation_ = request.generation;
    publishVideoInfo(request, source_->info());
    const double duration = source_->info().duration_seconds.value_or(
        request.initial_timestamp_seconds);
    const double target = std::clamp(
        request.initial_timestamp_seconds, 0.0, std::max(0.0, duration));
    if (target > 0.0) {
      (void)source_->seekToSeconds(target);
    }
    if (!isCurrent(request)) {
      return;
    }
    while (auto frame = source_->read()) {
      if (!isCurrent(request)) {
        return;
      }
      const double timestamp = relativeTimestampSeconds(*frame, source_->info());
      appendDecoded(std::move(frame));
      if (timestamp >= target - 1.0e-9) {
        break;
      }
    }
    if (!cache_.empty()) {
      publishCurrent(request);
    } else {
      finishLoading(request);
    }
  }

  void handle(const SeekRequest& request) {
    if (!source_ || active_generation_ != request.generation
        || !isCurrent(request)) {
      finishLoading(request);
      return;
    }
    const double duration = source_->info().duration_seconds.value_or(
        request.timestamp_seconds);
    const double target = std::clamp(
        request.timestamp_seconds, 0.0, std::max(0.0, duration));
    if (!source_->seekToSeconds(target)) {
      throw std::runtime_error("Video source does not support seeking");
    }
    if (!isCurrent(request)) {
      return;
    }
    cache_.clear();
    cache_position_ = 0U;
    while (auto frame = source_->read()) {
      if (!isCurrent(request)) {
        return;
      }
      const double timestamp = relativeTimestampSeconds(*frame, source_->info());
      appendDecoded(std::move(frame));
      if (timestamp >= target - 1.0e-9) {
        break;
      }
    }
    if (cache_.empty()) {
      finishLoading(request);
    } else {
      publishCurrent(request);
    }
  }

  void handle(const StepRequest& request) {
    if (!source_ || active_generation_ != request.generation || cache_.empty()
        || !isCurrent(request)) {
      finishLoading(request);
      return;
    }
    const int direction = request.delta < 0 ? -1 : 1;
    const int count = std::abs(request.delta);
    for (int index = 0; index < count; ++index) {
      if (!isCurrent(request) || !stepOnce(direction, request)) {
        break;
      }
    }
    publishCurrent(request);
  }

  bool stepOnce(int direction, const StepRequest& request) {
    if (direction > 0) {
      if (cache_position_ + 1U < cache_.size()) {
        ++cache_position_;
        return true;
      }
      return appendDecoded(source_->read());
    }

    if (cache_position_ > 0U) {
      --cache_position_;
      return true;
    }

    const double current_time = cache_.front().timestamp_seconds;
    if (current_time <= 1.0e-9) {
      return false;
    }
    const double fps = source_->info().framesPerSecond().value_or(30.0);
    const double lookback_seconds = std::max(
        0.5,
        static_cast<double>(preview_cache_capacity + 4U) / std::max(fps, 1.0));
    if (!source_->seekToSeconds(std::max(0.0, current_time - lookback_seconds))) {
      return false;
    }
    if (!isCurrent(request)) {
      return false;
    }
    cache_.clear();
    cache_position_ = 0U;
    while (auto frame = source_->read()) {
      if (!isCurrent(request)) {
        return false;
      }
      const double time = relativeTimestampSeconds(*frame, source_->info());
      appendDecoded(std::move(frame));
      if (time >= current_time - 1.0e-9) {
        break;
      }
    }
    if (cache_.size() > 1U) {
      cache_position_ = cache_.size() - 2U;
      return true;
    }
    if (!cache_.empty()) {
      cache_position_ = 0U;
    }
    return false;
  }

  template <typename Navigation>
  void requestNavigation(Navigation request) {
    {
      std::lock_guard lock{mutex_};
      request.generation = requested_generation_;
      if constexpr (std::is_same_v<Navigation, SeekRequest>) {
        // The user's latest absolute position supersedes every older intent.
        request.navigation_revision = ++requested_navigation_revision_;
        pending_navigation_.clear();
      } else {
        request.navigation_revision = requested_navigation_revision_;
        if constexpr (std::is_same_v<Navigation, StepRequest>) {
          if (!pending_navigation_.empty()) {
            if (auto* pending = std::get_if<StepRequest>(
                    &pending_navigation_.back());
                pending != nullptr
                && pending->generation == request.generation
                && pending->navigation_revision == request.navigation_revision) {
              pending->delta = std::clamp(
                  pending->delta + request.delta,
                  -maximum_pending_step_delta,
                  maximum_pending_step_delta);
              snapshot_.loading = true;
              snapshot_.frame.reset();
              snapshot_.error.clear();
              wake_.notify_one();
              return;
            }
          }
        }
      }
      pending_navigation_.push_back(std::move(request));
      snapshot_.loading = true;
      snapshot_.frame.reset();
      snapshot_.error.clear();
    }
    wake_.notify_one();
  }

  template <typename RequestType>
  [[nodiscard]] bool isCurrent(const RequestType& request) {
    std::lock_guard lock{mutex_};
    return isCurrentLocked(request);
  }

  template <typename RequestType>
  [[nodiscard]] bool isCurrentLocked(const RequestType& request) const {
    if (request.generation != requested_generation_) {
      return false;
    }
    if constexpr (std::is_same_v<RequestType, OpenRequest>) {
      return true;
    } else {
      return request.navigation_revision == requested_navigation_revision_;
    }
  }

  bool appendDecoded(std::optional<DecodedFrame> frame) {
    if (!frame) {
      return false;
    }
    ScrubFrame preview{
        frame->previewBgr(1280, 720),
        frame->decoded_frame_index,
        relativeTimestampSeconds(*frame, source_->info())};
    if (cache_.size() == preview_cache_capacity) {
      cache_.pop_front();
      if (cache_position_ > 0U) {
        --cache_position_;
      }
    }
    cache_.push_back(std::move(preview));
    cache_position_ = cache_.size() - 1U;
    return true;
  }

  void publishVideoInfo(const OpenRequest& request, const VideoInfo& video_info) {
    std::lock_guard lock{mutex_};
    if (!isCurrentLocked(request)) {
      return;
    }
    snapshot_.video_info = video_info;
  }

  template <typename RequestType>
  void publishCurrent(const RequestType& request) {
    std::lock_guard lock{mutex_};
    if (!isCurrentLocked(request)) {
      return;
    }
    snapshot_.loading = !pending_navigation_.empty();
    if constexpr (std::is_same_v<RequestType, OpenRequest>) {
      if (snapshot_.loading) {
        return;
      }
    }
    snapshot_.frame = cache_[cache_position_];
  }

  template <typename RequestType>
  void finishLoading(const RequestType& request) {
    std::lock_guard lock{mutex_};
    if (!isCurrentLocked(request)) {
      return;
    }
    snapshot_.loading = !pending_navigation_.empty();
  }

  void publishError(const Request& request, const std::string& error) {
    std::lock_guard lock{mutex_};
    const bool current = std::visit(
        [this](const auto& value) { return isCurrentLocked(value); },
        request);
    if (!current) {
      return;
    }
    pending_navigation_.clear();
    snapshot_.loading = false;
    snapshot_.error = error;
  }

  static constexpr std::size_t preview_cache_capacity = 24U;
  static constexpr int maximum_pending_step_delta = 120;
  std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_{};
  SourceGeneration requested_generation_{};
  NavigationRevision requested_navigation_revision_{};
  std::optional<SourceGeneration> active_generation_;
  std::optional<OpenRequest> pending_open_;
  std::deque<Request> pending_navigation_;
  ScrubberSnapshot snapshot_;
  ScrubSourceFactory source_factory_;
  std::unique_ptr<FrameSource> source_;
  std::deque<ScrubFrame> cache_;
  std::size_t cache_position_{};
  std::thread worker_;
};

VideoScrubber::VideoScrubber()
    : VideoScrubber{[](const std::filesystem::path& input_video) {
        return std::make_unique<VideoDecoder>(
            input_video,
            VideoDecoderOptions{.defer_full_bgr = true});
      }} {}

VideoScrubber::VideoScrubber(ScrubSourceFactory source_factory)
    : impl_{std::make_unique<Impl>(std::move(source_factory))} {}
VideoScrubber::~VideoScrubber() = default;

void VideoScrubber::open(
    std::filesystem::path input_video,
    double initial_timestamp_seconds) {
  impl_->open(std::move(input_video), initial_timestamp_seconds);
}
void VideoScrubber::seek(double timestamp_seconds) {
  impl_->seek(timestamp_seconds);
}
void VideoScrubber::stepForward() { impl_->step(1); }
void VideoScrubber::stepBackward() { impl_->step(-1); }
ScrubberSnapshot VideoScrubber::takeSnapshot() { return impl_->takeSnapshot(); }

}  // namespace frame_extractor::gui
