#include "manual_frame_exporter.hpp"

#include <catch2/catch_test_macros.hpp>

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace fe = frame_extractor;
namespace gui = frame_extractor::gui;

namespace {

struct TestDirectory {
  explicit TestDirectory(std::string_view label)
      : path{std::filesystem::temp_directory_path()
             / (std::string{label} + '_'
                + std::to_string(
                    std::chrono::steady_clock::now().time_since_epoch().count()))} {}

  ~TestDirectory() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  std::filesystem::path path;
};

struct FakeSourceState {
  std::mutex mutex;
  std::condition_variable changed;
  bool block_reads{};
  bool read_started{};
  bool release_read{};
  double seek_target{-1.0};
  std::vector<double> timestamps{2.75, 3.0, 3.25};
};

class FakeSource final : public fe::FrameSource {
 public:
  explicit FakeSource(std::shared_ptr<FakeSourceState> state)
      : state_{std::move(state)} {
    info_.width = 31;
    info_.height = 19;
    info_.time_base = {1, 1000};
    info_.average_frame_rate = {4, 1};
    info_.duration_seconds = 4.0;
  }

  [[nodiscard]] const fe::VideoInfo& info() const override { return info_; }

  [[nodiscard]] std::optional<fe::DecodedFrame> read() override {
    {
      std::unique_lock lock{state_->mutex};
      state_->read_started = true;
      state_->changed.notify_all();
      state_->changed.wait(lock, [this] {
        return !state_->block_reads || state_->release_read;
      });
    }
    if (next_frame_ >= state_->timestamps.size()) {
      return std::nullopt;
    }
    const double timestamp = state_->timestamps[next_frame_++];
    fe::DecodedFrame frame;
    frame.bgr = cv::Mat(
        info_.height,
        info_.width,
        CV_8UC3,
        cv::Scalar{20.0, 80.0, 140.0});
    frame.decoded_frame_index = static_cast<std::int64_t>(
        std::llround(timestamp * 4.0));
    frame.pts = static_cast<std::int64_t>(std::llround(timestamp * 1000.0));
    frame.time_base = info_.time_base;
    return frame;
  }

  bool seekToSeconds(double seconds) override {
    std::lock_guard lock{state_->mutex};
    state_->seek_target = seconds;
    next_frame_ = 0U;
    return true;
  }

 private:
  std::shared_ptr<FakeSourceState> state_;
  fe::VideoInfo info_;
  std::size_t next_frame_{};
};

gui::ManualFrameExportSnapshot waitForTerminal(gui::ManualFrameExporter& exporter) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
  while (std::chrono::steady_clock::now() < deadline) {
    auto snapshot = exporter.takeSnapshot();
    if (snapshot.phase == gui::ManualFrameExportPhase::complete
        || snapshot.phase == gui::ManualFrameExportPhase::failed) {
      return snapshot;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{5});
  }
  throw std::runtime_error("Timed out waiting for manual frame export");
}

}  // namespace

TEST_CASE("manual frame exporter seeks and saves the first frame at or after the target") {
  TestDirectory output{"frame_extractor_manual_export"};
  const auto state = std::make_shared<FakeSourceState>();
  gui::ManualFrameExporter exporter{
      [state](const std::filesystem::path&) {
        return std::make_unique<FakeSource>(state);
      }};

  REQUIRE(exporter.start(
      output.path / "film?cut.mov",
      output.path,
      3.1,
      fe::ImageFormat::png));
  const auto snapshot = waitForTerminal(exporter);
  REQUIRE(snapshot.phase == gui::ManualFrameExportPhase::complete);
  REQUIRE(snapshot.decoded_frame_index);
  REQUIRE(snapshot.timestamp_seconds);
  CHECK(*snapshot.decoded_frame_index == 13);
  CHECK(*snapshot.timestamp_seconds == 3.25);
  const auto source_directory =
      snapshot.output_path.parent_path().filename().string();
  CHECK(source_directory.starts_with("film_cut-"));
  CHECK(source_directory.size() == std::string{"film_cut-"}.size() + 16U);
  CHECK(snapshot.output_path.filename() == "frame_000013.png");
  CHECK_FALSE(snapshot.already_exists);
  CHECK(state->seek_target == 3.1);

  const cv::Mat saved = cv::imread(snapshot.output_path.string(), cv::IMREAD_COLOR);
  REQUIRE_FALSE(saved.empty());
  CHECK(saved.cols == 31);
  CHECK(saved.rows == 19);
}

TEST_CASE("manual frame exporter separates videos that share a filename") {
  TestDirectory output{"frame_extractor_manual_sources"};
  const auto state = std::make_shared<FakeSourceState>();
  gui::ManualFrameExporter exporter{
      [state](const std::filesystem::path&) {
        return std::make_unique<FakeSource>(state);
      }};

  REQUIRE(exporter.start(
      output.path / "first" / "video.mov",
      output.path,
      3.0,
      fe::ImageFormat::jpg));
  const auto first = waitForTerminal(exporter);
  REQUIRE(first.phase == gui::ManualFrameExportPhase::complete);
  exporter.reset();
  REQUIRE(exporter.start(
      output.path / "second" / "video.mov",
      output.path,
      3.0,
      fe::ImageFormat::jpg));
  const auto second = waitForTerminal(exporter);
  REQUIRE(second.phase == gui::ManualFrameExportPhase::complete);

  CHECK(first.output_path.filename() == second.output_path.filename());
  CHECK(first.output_path.parent_path() != second.output_path.parent_path());
  CHECK(std::filesystem::is_regular_file(first.output_path));
  CHECK(std::filesystem::is_regular_file(second.output_path));
}

TEST_CASE("manual frame exporter neutralizes Windows device names") {
  TestDirectory output{"frame_extractor_manual_reserved_name"};
  const auto state = std::make_shared<FakeSourceState>();
  gui::ManualFrameExporter exporter{
      [state](const std::filesystem::path&) {
        return std::make_unique<FakeSource>(state);
      }};

  REQUIRE(exporter.start(
      "con.archive.mov", output.path, 3.0, fe::ImageFormat::jpg));
  const auto snapshot = waitForTerminal(exporter);
  REQUIRE(snapshot.phase == gui::ManualFrameExportPhase::complete);
  CHECK(snapshot.output_path.parent_path().filename().string().starts_with(
      "_con.archive-"));
}

TEST_CASE("manual frame exporter treats an existing capture as success without rewriting it") {
  TestDirectory output{"frame_extractor_manual_collision"};
  const auto state = std::make_shared<FakeSourceState>();
  std::size_t source_factory_calls{};
  gui::ManualFrameExporter exporter{
      [state, &source_factory_calls](const std::filesystem::path&) {
        ++source_factory_calls;
        return std::make_unique<FakeSource>(state);
      }};

  REQUIRE(exporter.start(
      "video.mov", output.path, 3.0, fe::ImageFormat::jpg));
  const auto first = waitForTerminal(exporter);
  REQUIRE(first.phase == gui::ManualFrameExportPhase::complete);
  REQUIRE_FALSE(first.already_exists);
  {
    std::ofstream sentinel{first.output_path, std::ios::binary | std::ios::trunc};
    REQUIRE(sentinel.good());
    sentinel << "existing manual frame";
  }
  exporter.reset();
  REQUIRE(exporter.start(
      "video.mov", output.path, 3.0, fe::ImageFormat::jpg));
  const auto second = waitForTerminal(exporter);
  REQUIRE(second.phase == gui::ManualFrameExportPhase::complete);

  CHECK(first.output_path.filename() == "frame_000012.jpg");
  CHECK(second.output_path == first.output_path);
  CHECK(second.already_exists);
  CHECK(source_factory_calls == 2U);
  CHECK(std::filesystem::is_regular_file(first.output_path));
  std::ifstream contents{second.output_path, std::ios::binary};
  CHECK(std::string{
      std::istreambuf_iterator<char>{contents},
      std::istreambuf_iterator<char>{}} == "existing manual frame");
}

TEST_CASE("manual frame exporter rejects another export while busy") {
  TestDirectory output{"frame_extractor_manual_busy"};
  const auto state = std::make_shared<FakeSourceState>();
  state->block_reads = true;
  gui::ManualFrameExporter exporter{
      [state](const std::filesystem::path&) {
        return std::make_unique<FakeSource>(state);
      }};

  REQUIRE(exporter.start(
      "video.mov", output.path, 3.0, fe::ImageFormat::jpg));
  bool read_started = false;
  {
    std::unique_lock lock{state->mutex};
    read_started = state->changed.wait_for(
        lock, std::chrono::seconds{3}, [state] { return state->read_started; });
  }
  CHECK_FALSE(exporter.start(
      "other.mov", output.path, 1.0, fe::ImageFormat::png));
  {
    std::lock_guard lock{state->mutex};
    state->release_read = true;
  }
  state->changed.notify_all();
  REQUIRE(read_started);
  CHECK(waitForTerminal(exporter).phase == gui::ManualFrameExportPhase::complete);
}

TEST_CASE("manual frame exporter reports source failures") {
  TestDirectory output{"frame_extractor_manual_failure"};
  gui::ManualFrameExporter exporter{
      [](const std::filesystem::path&) -> std::unique_ptr<fe::FrameSource> {
        throw std::runtime_error("decoder unavailable");
      }};

  REQUIRE(exporter.start(
      "video.mov", output.path, 1.0, fe::ImageFormat::jpg));
  const auto snapshot = waitForTerminal(exporter);
  CHECK(snapshot.phase == gui::ManualFrameExportPhase::failed);
  CHECK(snapshot.error.find("decoder unavailable") != std::string::npos);
  CHECK(snapshot.output_path.empty());
}
