#include "video_scrubber.hpp"
#include "fixture_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace gui = frame_extractor::gui;
using ScrubberFixture = frame_extractor::test::MaterializedFixture;

namespace {

struct ControlledScrubState {
  bool waitForSeekCount(std::size_t expected) {
    std::unique_lock lock{mutex};
    return changed.wait_for(lock, std::chrono::seconds{3}, [this, expected] {
      return started_seeks >= expected;
    });
  }

  void releaseSeeksThrough(std::size_t count) {
    {
      std::lock_guard lock{mutex};
      released_seeks = std::max(released_seeks, count);
    }
    changed.notify_all();
  }

  void releaseAll() {
    {
      std::lock_guard lock{mutex};
      release_all = true;
    }
    changed.notify_all();
  }

  std::mutex mutex;
  std::condition_variable changed;
  std::size_t started_seeks{};
  std::size_t released_seeks{};
  bool release_all{};
  bool frame_pending{true};
  double frame_timestamp_seconds{};
};

struct BlockingSequenceState {
  bool waitUntilBlocked() {
    std::unique_lock lock{mutex};
    return changed.wait_for(lock, std::chrono::seconds{3}, [this] {
      return blocked;
    });
  }

  void release() {
    {
      std::lock_guard lock{mutex};
      released = true;
    }
    changed.notify_all();
  }

  std::mutex mutex;
  std::condition_variable changed;
  bool blocked{};
  bool released{};
  std::int64_t next_frame{};
};

class BlockingSequenceSource final : public frame_extractor::FrameSource {
 public:
  explicit BlockingSequenceSource(std::shared_ptr<BlockingSequenceState> state)
      : state_{std::move(state)} {
    info_.width = 2;
    info_.height = 2;
    info_.time_base = {1, 30};
    info_.average_frame_rate = {30, 1};
    info_.duration_seconds = 1'000.0;
  }

  [[nodiscard]] const frame_extractor::VideoInfo& info() const override {
    return info_;
  }

  [[nodiscard]] std::optional<frame_extractor::DecodedFrame> read() override {
    std::int64_t frame_index = 0;
    {
      std::unique_lock lock{state_->mutex};
      if (state_->next_frame == 1 && !state_->released) {
        state_->blocked = true;
        state_->changed.notify_all();
        state_->changed.wait(lock, [this] { return state_->released; });
      }
      frame_index = state_->next_frame++;
    }
    frame_extractor::DecodedFrame frame;
    frame.bgr = cv::Mat::zeros(2, 2, CV_8UC3);
    frame.decoded_frame_index = frame_index;
    frame.pts = frame_index;
    frame.time_base = {1, 30};
    return frame;
  }

 private:
  std::shared_ptr<BlockingSequenceState> state_;
  frame_extractor::VideoInfo info_;
};

class ControlledScrubSource final : public frame_extractor::FrameSource {
 public:
  explicit ControlledScrubSource(std::shared_ptr<ControlledScrubState> state)
      : state_{std::move(state)} {
    info_.width = 2;
    info_.height = 2;
    info_.time_base = {1, 1000};
    info_.average_frame_rate = {30, 1};
    info_.duration_seconds = 10.0;
  }

  [[nodiscard]] const frame_extractor::VideoInfo& info() const override {
    return info_;
  }

  [[nodiscard]] std::optional<frame_extractor::DecodedFrame> read() override {
    double timestamp_seconds = 0.0;
    {
      std::lock_guard lock{state_->mutex};
      if (!state_->frame_pending) {
        return std::nullopt;
      }
      state_->frame_pending = false;
      timestamp_seconds = state_->frame_timestamp_seconds;
    }

    frame_extractor::DecodedFrame frame;
    frame.bgr = cv::Mat::zeros(2, 2, CV_8UC3);
    frame.decoded_frame_index = static_cast<std::int64_t>(
        std::llround(timestamp_seconds * 30.0));
    frame.pts = static_cast<std::int64_t>(
        std::llround(timestamp_seconds * 1000.0));
    frame.time_base = {1, 1000};
    return frame;
  }

  bool seekToSeconds(double seconds) override {
    std::unique_lock lock{state_->mutex};
    const std::size_t seek_number = ++state_->started_seeks;
    state_->changed.notify_all();
    state_->changed.wait(lock, [this, seek_number] {
      return state_->release_all || state_->released_seeks >= seek_number;
    });
    state_->frame_timestamp_seconds = seconds;
    state_->frame_pending = true;
    return true;
  }

 private:
  std::shared_ptr<ControlledScrubState> state_;
  frame_extractor::VideoInfo info_;
};

gui::ScrubFrame waitForFrame(gui::VideoScrubber& scrubber) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (std::chrono::steady_clock::now() < deadline) {
    auto snapshot = scrubber.takeSnapshot();
    if (!snapshot.error.empty()) {
      throw std::runtime_error(snapshot.error);
    }
    if (snapshot.frame) {
      return std::move(*snapshot.frame);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  throw std::runtime_error("timed out waiting for scrubber frame");
}

gui::ScrubberSnapshot waitForLoadedFrame(gui::VideoScrubber& scrubber) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (std::chrono::steady_clock::now() < deadline) {
    auto snapshot = scrubber.takeSnapshot();
    if (!snapshot.error.empty()) {
      throw std::runtime_error(snapshot.error);
    }
    if (snapshot.video_info && snapshot.frame) {
      return snapshot;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  throw std::runtime_error("timed out waiting for loaded scrubber frame");
}

gui::ScrubberSnapshot waitUntilIdle(gui::VideoScrubber& scrubber) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (std::chrono::steady_clock::now() < deadline) {
    auto snapshot = scrubber.takeSnapshot();
    if (!snapshot.loading) {
      return snapshot;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  throw std::runtime_error("timed out waiting for idle scrubber");
}

}  // namespace

TEST_CASE("video scrubber steps backward and forward through cached presentation frames") {
  const ScrubberFixture fixture{"cfr_h264.mp4"};
  gui::VideoScrubber scrubber;
  scrubber.open(fixture.path());
  CHECK(waitForFrame(scrubber).frame_index == 0);

  for (std::int64_t expected = 1; expected <= 6; ++expected) {
    scrubber.stepForward();
    CHECK(waitForFrame(scrubber).frame_index == expected);
  }
  for (std::int64_t expected = 5; expected >= 2; --expected) {
    scrubber.stepBackward();
    CHECK(waitForFrame(scrubber).frame_index == expected);
  }
  scrubber.stepForward();
  CHECK(waitForFrame(scrubber).frame_index == 3);
}

TEST_CASE("video scrubber preserves burst steps and seek-step ordering") {
  const ScrubberFixture fixture{"cfr_h264.mp4"};
  gui::VideoScrubber scrubber;
  scrubber.open(fixture.path());
  CHECK(waitForFrame(scrubber).frame_index == 0);

  for (int step = 0; step < 5; ++step) {
    scrubber.stepForward();
  }
  auto snapshot = waitUntilIdle(scrubber);
  REQUIRE(snapshot.frame);
  CHECK(snapshot.frame->frame_index == 5);

  for (int step = 0; step < 3; ++step) {
    scrubber.stepBackward();
  }
  snapshot = waitUntilIdle(scrubber);
  REQUIRE(snapshot.frame);
  CHECK(snapshot.frame->frame_index == 2);

  scrubber.seek(0.0);
  scrubber.stepForward();
  scrubber.stepForward();
  scrubber.stepForward();
  snapshot = waitUntilIdle(scrubber);
  REQUIRE(snapshot.frame);
  CHECK(snapshot.frame->frame_index == 3);
}

TEST_CASE("video scrubber opens directly at a restored playhead") {
  const auto state = std::make_shared<ControlledScrubState>();
  gui::VideoScrubber scrubber{
      [state](const std::filesystem::path&) {
        return std::make_unique<ControlledScrubSource>(state);
      }};

  scrubber.open("controlled-video", 4.25);
  const bool seek_started = state->waitForSeekCount(1U);
  if (!seek_started) {
    state->releaseAll();
  }
  REQUIRE(seek_started);
  CHECK_FALSE(scrubber.takeSnapshot().frame);
  state->releaseAll();
  CHECK(waitForFrame(scrubber).timestamp_seconds == 4.25);
}

TEST_CASE("video scrubber bounds repeated steps while decoding is blocked") {
  const auto state = std::make_shared<BlockingSequenceState>();
  gui::VideoScrubber scrubber{
      [state](const std::filesystem::path&) {
        return std::make_unique<BlockingSequenceSource>(state);
      }};
  scrubber.open("blocking-video");
  CHECK(waitForFrame(scrubber).frame_index == 0);

  scrubber.stepForward();
  const bool blocked = state->waitUntilBlocked();
  if (!blocked) {
    state->release();
  }
  REQUIRE(blocked);
  for (int step = 0; step < 1'000; ++step) {
    scrubber.stepForward();
  }
  state->release();

  const auto snapshot = waitUntilIdle(scrubber);
  REQUIRE(snapshot.frame);
  CHECK(snapshot.frame->frame_index == 121);
}

TEST_CASE("a newer seek suppresses a frame from a seek already in flight") {
  const auto state = std::make_shared<ControlledScrubState>();
  gui::VideoScrubber scrubber{
      [state](const std::filesystem::path&) {
        return std::make_unique<ControlledScrubSource>(state);
      }};
  scrubber.open("controlled-video");
  CHECK(waitForFrame(scrubber).timestamp_seconds == 0.0);

  scrubber.seek(8.0);
  const bool first_seek_started = state->waitForSeekCount(1U);
  if (!first_seek_started) {
    state->releaseAll();
  }
  REQUIRE(first_seek_started);

  scrubber.seek(2.0);
  state->releaseSeeksThrough(1U);
  const bool second_seek_started = state->waitForSeekCount(2U);
  if (!second_seek_started) {
    state->releaseAll();
  }
  REQUIRE(second_seek_started);

  const auto while_latest_seek_is_blocked = scrubber.takeSnapshot();
  state->releaseAll();
  CHECK(while_latest_seek_is_blocked.loading);
  CHECK_FALSE(while_latest_seek_is_blocked.frame);
  CHECK(waitForFrame(scrubber).timestamp_seconds == 2.0);
}

TEST_CASE("video scrubber rejects navigation before a video is open without staying busy") {
  gui::VideoScrubber scrubber;
  scrubber.stepBackward();
  const auto snapshot = waitUntilIdle(scrubber);
  CHECK_FALSE(snapshot.loading);
  CHECK_FALSE(snapshot.frame);
  CHECK(snapshot.error.empty());
}

TEST_CASE("opening a new video invalidates old snapshots before decoding") {
  const ScrubberFixture first{"cfr_h264.mp4"};
  const ScrubberFixture second{"odd_h264.mp4.b64"};
  gui::VideoScrubber scrubber;
  scrubber.open(first.path());
  const auto first_snapshot = waitForLoadedFrame(scrubber);
  REQUIRE(first_snapshot.video_info);
  CHECK(first_snapshot.video_info->width == 96);
  CHECK(first_snapshot.video_info->height == 64);

  scrubber.open(second.path());
  const auto invalidated = scrubber.takeSnapshot();
  CHECK(invalidated.loading);
  CHECK_FALSE(invalidated.video_info);
  CHECK_FALSE(invalidated.frame);

  const auto second_snapshot = waitForLoadedFrame(scrubber);
  REQUIRE(second_snapshot.video_info);
  REQUIRE(second_snapshot.frame);
  CHECK(second_snapshot.video_info->width == 95);
  CHECK(second_snapshot.video_info->height == 63);
  CHECK(second_snapshot.frame->bgr.cols == 95);
  CHECK(second_snapshot.frame->bgr.rows == 63);
}

TEST_CASE("navigation cannot replace a pending video open") {
  const ScrubberFixture first{"cfr_h264.mp4"};
  const ScrubberFixture second{"odd_h264.mp4.b64"};
  gui::VideoScrubber scrubber;
  scrubber.open(first.path());
  REQUIRE(waitForLoadedFrame(scrubber).video_info);

  scrubber.open(second.path());
  scrubber.seek(0.0);
  const auto snapshot = waitForLoadedFrame(scrubber);
  REQUIRE(snapshot.video_info);
  REQUIRE(snapshot.frame);
  CHECK(snapshot.video_info->width == 95);
  CHECK(snapshot.video_info->height == 63);
  CHECK(snapshot.frame->bgr.cols == 95);
  CHECK(snapshot.frame->bgr.rows == 63);
}
