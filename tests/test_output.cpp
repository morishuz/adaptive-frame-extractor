#include "frame_extractor/output.hpp"

#include "async_image_writer.hpp"
#include "frame_extractor/build_info.hpp"
#include "frame_extractor/detail/image_writer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <opencv2/imgcodecs.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace fe = frame_extractor;

namespace {

std::string readText(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary};
  REQUIRE(input.good());
  return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

bool hasSummaryHeading(
    std::string_view summary,
    std::string_view heading,
    char underline) {
  return summary.find(
             std::string{heading} + '\n'
             + std::string(heading.size(), underline) + '\n')
      != std::string_view::npos;
}

bool hasSummaryField(
    std::string_view summary,
    std::string_view label,
    std::string_view expected_value) {
  const std::string prefix = std::string{label} + ':';
  std::size_t line_start = 0U;
  while (line_start < summary.size()) {
    const std::size_t line_end = summary.find('\n', line_start);
    std::string_view line = summary.substr(
        line_start,
        line_end == std::string_view::npos
            ? summary.size() - line_start
            : line_end - line_start);
    if (line.starts_with(prefix)) {
      line.remove_prefix(prefix.size());
      const std::size_t value_start = line.find_first_not_of(' ');
      if (value_start != std::string_view::npos
          && line.substr(value_start) == expected_value) {
        return true;
      }
    }
    if (line_end == std::string_view::npos) {
      break;
    }
    line_start = line_end + 1U;
  }
  return false;
}

fe::ProcessingResult sampleResult() {
  fe::ProcessingResult result;
  result.processed_frames = 3;
  result.trigger_count = 0;
  result.pts_available_frames = 3;
  result.regions_processed = 1;
  result.timings.analysis_native_luma_frames = 2;
  result.timings.analysis_ffmpeg_fallback_frames = 1;
  result.selected_frames.push_back(fe::SelectedFrame{
      0,
      0,
      2,
      24,
      {1, 600},
      "first",
      "ok",
      fe::FrameScores{2, 0.04, 0.0, 1, 1.0}});
  result.selected_frames.push_back(fe::SelectedFrame{
      1,
      2,
      4,
      96,
      {1, 600},
      "final",
      "ok",
      fe::FrameScores{4, 0.16, 12.5, 1, 0.75}});
  result.processed_regions.push_back(fe::ProcessedRegion{
      0, fe::TimeRange{0.04, 0.16}, 0, 2, 2, 4, 0.04, 0.16, 3, 2});
  return result;
}

bool waitForFile(const std::filesystem::path& path) {
  for (int attempt = 0; attempt < 100; ++attempt) {
    if (std::filesystem::is_regular_file(path)) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
  }
  return false;
}

}  // namespace

TEST_CASE("output writer creates unique complete schema-v5 runs") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto base = std::filesystem::temp_directory_path()
      / ("frame_extractor_output_test_" + unique);
  fe::Config config;
  config.output.image_format = fe::ImageFormat::png;
  fe::VideoInfo info{16, 12, {1, 600}, {25, 1}, 5'000, "h264"};
  const auto result = sampleResult();
  const cv::Mat first_frame(12, 16, CV_8UC3, cv::Scalar{10, 20, 30});
  const cv::Mat final_frame(12, 16, CV_8UC3, cv::Scalar{40, 50, 60});
  fe::ProcessOptions options;
  options.input_label = "input.mov";
  options.regions = {{0.04, 0.16}};

  fe::RunOutputWriter first_writer{base, config};
  first_writer.onFrameSelected(result.selected_frames[0], first_frame);
  const auto first_image_path = first_writer.paths().keyframe_dir
      / "keyframe_0000_000002.png";
  CHECK(waitForFile(first_image_path));
  CHECK_FALSE(std::filesystem::exists(first_writer.paths().manifest_path));
  first_writer.onFrameSelected(result.selected_frames[1], final_frame);
  first_writer.finalize(
      "input.mov", options, info, result, std::chrono::duration<double>{0.5});

  fe::RunOutputWriter second_writer{base, fe::Config{}};
  second_writer.onFrameSelected(result.selected_frames[0], first_frame);
  second_writer.onFrameSelected(result.selected_frames[1], final_frame);
  auto whole_video_options = options;
  whole_video_options.regions.clear();
  second_writer.finalize(
      "input.mov",
      whole_video_options,
      info,
      result,
      std::chrono::duration<double>{0.5});
  const auto& first = first_writer.paths();
  const auto& second = second_writer.paths();
  CHECK(first.run_dir != second.run_dir);
  CHECK(std::filesystem::is_regular_file(first.config_path));
  CHECK(std::filesystem::is_regular_file(first.manifest_path));
  CHECK(std::filesystem::is_regular_file(first.summary_path));

  const auto manifest = readText(first.manifest_path);
  CHECK(manifest.starts_with(
      "filename,keyframe_index,source_frame_index,region_index,pts,pos_seconds_raw,"
      "timing_status,selection_reason,motion_score_px,in_bounds_ratio\n"));
  CHECK(manifest.find("keyframes/keyframe_0000_000002.png,0,2,0,24,0.04,ok,first,0,1")
        != std::string::npos);
  CHECK(manifest.find("keyframes/keyframe_0001_000004.png,1,4,0,96,0.16,ok,final,12.5,0.75")
        != std::string::npos);

  const auto first_image = cv::imread(
      (first.keyframe_dir / "keyframe_0000_000002.png").string(), cv::IMREAD_COLOR);
  REQUIRE_FALSE(first_image.empty());
  CHECK(first_image.cols == 16);
  CHECK(first_image.rows == 12);
  CHECK(first_image.at<cv::Vec3b>(0, 0) == cv::Vec3b{10, 20, 30});

  const auto summary = readText(first.summary_path);
  CHECK(hasSummaryHeading(summary, "Frame Extractor - Run Summary", '='));
  CHECK(hasSummaryHeading(summary, "Result", '-'));
  CHECK(hasSummaryField(summary, "Status", "Completed"));
  CHECK(hasSummaryField(summary, "Frames processed", "3"));
  CHECK(hasSummaryField(summary, "Keyframes saved", "2"));
  CHECK(hasSummaryField(summary, "Trigger events", "0"));
  CHECK(hasSummaryField(summary, "Average spacing", "1.50 frames per keyframe"));
  CHECK(hasSummaryField(summary, "Selection rate", "66.67%"));
  CHECK(hasSummaryField(summary, "Regions processed", "1 of 1"));

  CHECK(hasSummaryHeading(summary, "Input", '-'));
  CHECK(hasSummaryField(summary, "Video", "input.mov"));
  CHECK(hasSummaryField(summary, "Scope", "Selected regions"));
  CHECK(hasSummaryField(summary, "Selected duration", "0.120 s"));
  CHECK(summary.find("Starting frame:") == std::string::npos);
  CHECK(summary.find("Maximum frames:") == std::string::npos);

  CHECK(hasSummaryHeading(summary, "Extraction Settings", '-'));
  CHECK(hasSummaryField(summary, "Selection", "Adaptive (Custom profile)"));
  CHECK(hasSummaryField(summary, "Image format", "PNG, compression level 1"));
  CHECK(hasSummaryField(summary, "Keyframe layout", "Single directory"));
  CHECK(hasSummaryField(summary, "Manifest", "keyframes.csv (schema 5)"));

  CHECK(hasSummaryHeading(summary, "Regions", '-'));
  CHECK(summary.find("Region 1\n") != std::string::npos);
  CHECK(hasSummaryField(
      summary,
      "  Time",
      "00:00.040 - 00:00.160 (0.120 s)"));
  CHECK(hasSummaryField(summary, "  Source frames", "2 - 4"));
  CHECK(hasSummaryField(summary, "  Frames processed", "3"));
  CHECK(hasSummaryField(summary, "  Keyframes saved", "2"));
  CHECK(hasSummaryField(
      summary,
      "  Average spacing",
      "1.50 frames per keyframe"));

  CHECK(hasSummaryHeading(summary, "Performance", '-'));
  CHECK(summary.find("Runtime:") != std::string::npos);
  CHECK(summary.find("Processing throughput:") != std::string::npos);
  CHECK(hasSummaryField(summary, "Accounted time", "0.000 ms (0.00%)"));
  CHECK(summary.find("Other time:") != std::string::npos);
  CHECK(summary.find("(100.00%)") != std::string::npos);

  CHECK(hasSummaryHeading(summary, "Pipeline Timing", '-'));
  CHECK(hasSummaryField(summary, "Source reading", "0.000 ms"));
  CHECK(hasSummaryField(summary, "Dense optical flow", "0.000 ms"));
  CHECK(hasSummaryHeading(summary, "Source Reading Breakdown", '-'));
  CHECK(hasSummaryField(summary, "Hardware transfer", "0.000 ms"));

  CHECK(hasSummaryHeading(summary, "Decoder and Analysis", '-'));
  CHECK(hasSummaryField(summary, "Backend", "libav"));
  CHECK(hasSummaryField(summary, "Codec", "h264"));
  CHECK(hasSummaryField(summary, "Hardware decoding", "No"));
  CHECK(hasSummaryField(summary, "Native-luma frames", "2"));
  CHECK(hasSummaryField(summary, "FFmpeg fallback frames", "1"));

  CHECK(hasSummaryHeading(summary, "Video Details", '-'));
  CHECK(hasSummaryField(summary, "Frame rate", "25.00 fps"));
  CHECK(hasSummaryField(summary, "Reported frame count", "5,000"));
  CHECK(hasSummaryField(summary, "Duration", "Unavailable"));
  CHECK(hasSummaryField(summary, "Start time", "Unavailable"));
  CHECK(hasSummaryField(summary, "Rotation", "0 degrees"));
  CHECK(hasSummaryField(summary, "PTS time base", "1/600"));

  CHECK(hasSummaryHeading(summary, "Timing Validation", '-'));
  CHECK(hasSummaryField(summary, "Status", "OK"));
  CHECK(hasSummaryField(summary, "PTS available", "3"));
  CHECK(hasSummaryField(summary, "PTS unavailable", "0"));
  CHECK(hasSummaryField(summary, "Non-monotonic PTS", "0"));
  CHECK(hasSummaryField(summary, "Exact indices on seek", "No"));
  CHECK(hasSummaryHeading(summary, "Software", '-'));
  CHECK(hasSummaryField(summary, "Version", fe::build::version));
  CHECK(hasSummaryField(summary, "Build", fe::build::id));
  CHECK(summary.find("OpenCV:") != std::string::npos);

  CHECK(summary.find("selection_mode:") == std::string::npos);
  CHECK(summary.find("processed_frames:") == std::string::npos);
  CHECK(summary.find("profiling_") == std::string::npos);
  CHECK(summary.find("raw_pos_timeline_valid:") == std::string::npos);
  CHECK(summary.find("None") == std::string::npos);

  const auto jpeg_summary = readText(second.summary_path);
  CHECK(hasSummaryField(jpeg_summary, "Image format", "JPEG, quality 95"));
  CHECK(hasSummaryField(jpeg_summary, "Scope", "Entire video"));
  CHECK(jpeg_summary.find("compression level") == std::string::npos);
  CHECK_FALSE(hasSummaryHeading(jpeg_summary, "Regions", '-'));
  CHECK(jpeg_summary.find("Region 1\n") == std::string::npos);
  CHECK(jpeg_summary.find("Selected duration:") == std::string::npos);
  CHECK_THROWS_AS(
      first_writer.finalize(
          "input.mov",
          options,
          info,
          result,
          std::chrono::duration<double>{0.5}),
      std::logic_error);
  std::filesystem::remove_all(base);
}

TEST_CASE("output writer optionally groups keyframes in one-based region directories") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto base = std::filesystem::temp_directory_path()
      / ("frame_extractor_grouped_output_test_" + unique);
  fe::Config config;
  config.output.image_format = fe::ImageFormat::png;
  fe::VideoInfo info{16, 12, {1, 600}, {25, 1}, 5, "h264"};
  auto result = sampleResult();
  result.processed_frames = 2;
  result.regions_processed = 2;
  result.selected_frames[1].processed_index = 1;
  result.selected_frames[1].region_index = 1;
  result.processed_regions = {
      {0, fe::TimeRange{0.04, 0.04}, 0, 0, 2, 2, 0.04, 0.04, 1, 1},
      {1, fe::TimeRange{0.16, 0.16}, 1, 1, 4, 4, 0.16, 0.16, 1, 1}};
  fe::ProcessOptions options;
  options.regions = {{0.04, 0.04}, {0.16, 0.16}};
  options.fixed_frame_interval = 8U;
  options.selection_profile = "fixed";
  const cv::Mat frame(12, 16, CV_8UC3, cv::Scalar{10, 20, 30});

  fe::RunOutputWriter writer{
      base,
      config,
      fe::RunOutputOptions{.group_keyframes_by_region = true}};
  writer.onFrameSelected(result.selected_frames[0], frame);
  writer.onFrameSelected(result.selected_frames[1], frame);

  writer.finalize(
      "input.mov", options, info, result, std::chrono::duration<double>{0.5});

  const auto& paths = writer.paths();
  CHECK(std::filesystem::is_regular_file(
      paths.keyframe_dir / "region_01" / "keyframe_0000_000002.png"));
  CHECK(std::filesystem::is_regular_file(
      paths.keyframe_dir / "region_02" / "keyframe_0001_000004.png"));

  const auto manifest = readText(paths.manifest_path);
  CHECK(manifest.find("keyframes/region_01/keyframe_0000_000002.png")
        != std::string::npos);
  CHECK(manifest.find("keyframes/region_02/keyframe_0001_000004.png")
        != std::string::npos);
  const auto summary = readText(paths.summary_path);
  CHECK(hasSummaryField(
      summary, "Selection", "Fixed interval (every 8th frame)"));
  CHECK(hasSummaryField(
      summary, "Keyframe layout", "One directory per region"));
  CHECK(hasSummaryField(summary, "Regions processed", "2 of 2"));
  CHECK(summary.find("Region 1\n") != std::string::npos);
  CHECK(summary.find("Region 2\n") != std::string::npos);

  std::filesystem::remove_all(base);
}

TEST_CASE("output writer marks a finalized partial run as failed") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto base = std::filesystem::temp_directory_path()
      / ("frame_extractor_failed_output_test_" + unique);
  const auto result = sampleResult();
  const cv::Mat frame(12, 16, CV_8UC3, cv::Scalar{10, 20, 30});

  fe::RunOutputWriter writer{base, fe::Config{}};
  writer.onFrameSelected(result.selected_frames[0], frame);
  writer.onFrameSelected(result.selected_frames[1], frame);
  writer.finalize(
      "input.mov",
      fe::ProcessOptions{},
      fe::VideoInfo{},
      result,
      std::chrono::duration<double>{0.5},
      "decoder stopped unexpectedly");

  const auto summary = readText(writer.paths().summary_path);
  CHECK(hasSummaryField(summary, "Status", "Failed"));
  CHECK(hasSummaryField(summary, "Error", "decoder stopped unexpectedly"));
  std::filesystem::remove_all(base);
}

TEST_CASE("output writer can discard an empty run") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto base = std::filesystem::temp_directory_path()
      / ("frame_extractor_empty_output_test_" + unique);
  fe::RunOutputWriter writer{base, fe::Config{}};
  const auto run_directory = writer.paths().run_dir;
  REQUIRE(std::filesystem::is_directory(run_directory));

  writer.discardEmpty();

  CHECK_FALSE(std::filesystem::exists(run_directory));
  CHECK_THROWS_AS(writer.discardEmpty(), std::logic_error);
  std::filesystem::remove_all(base);
}

TEST_CASE("image writer supports Unicode filesystem paths") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto base = std::filesystem::temp_directory_path()
      / ("frame_extractor_unicode_output_test_" + unique);
  const auto image_path = base / std::filesystem::path{u8"Bilder-\u5F71"}
      / std::filesystem::path{u8"Schl\u00FCsselbild.png"};
  const cv::Mat frame(12, 16, CV_8UC3, cv::Scalar{10, 20, 30});

  fe::detail::ImageFileWriter{fe::ImageFormat::png}.writeAtomically(
      image_path, frame);

  CHECK(std::filesystem::is_regular_file(image_path));
  CHECK(std::filesystem::file_size(image_path) > 0U);
  std::filesystem::remove_all(base);
}

TEST_CASE("asynchronous output writer propagates background failures") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto base = std::filesystem::temp_directory_path()
      / ("frame_extractor_output_failure_test_" + unique);
  const cv::Mat frame(12, 16, CV_8UC3, cv::Scalar{10, 20, 30});

  {
    fe::RunOutputWriter writer{base, fe::Config{}};
    std::filesystem::remove_all(writer.paths().keyframe_dir);
    std::ofstream blocking_file{writer.paths().keyframe_dir};
    REQUIRE(blocking_file.good());
    blocking_file << "not a directory";
    blocking_file.close();

    writer.onFrameSelected(sampleResult().selected_frames.front(), frame);
    CHECK_THROWS_AS(
        writer.finalize(
            "input.mov",
            fe::ProcessOptions{},
            fe::VideoInfo{},
            sampleResult(),
            std::chrono::duration<double>{0.5}),
        std::runtime_error);
  }

  std::filesystem::remove_all(base);
}

TEST_CASE("shared writer pool completes a burst of JPEG images") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto base = std::filesystem::temp_directory_path()
      / ("frame_extractor_jpeg_writer_pool_test_" + unique);
  constexpr std::size_t image_count = 12U;

  std::filesystem::path keyframe_dir;
  {
    fe::RunOutputWriter writer{base, fe::Config{}};
    keyframe_dir = writer.paths().keyframe_dir;
    for (std::size_t index = 0; index < image_count; ++index) {
      fe::SelectedFrame selected;
      selected.keyframe_index = index;
      selected.decoded_frame_index = static_cast<std::int64_t>(index);
      const cv::Mat frame(
          48,
          64,
          CV_8UC3,
          cv::Scalar{
              static_cast<double>(index),
              static_cast<double>(index + 1U),
              static_cast<double>(index + 2U)});
      writer.onFrameSelected(selected, frame);
    }
  }

  std::size_t written_images = 0U;
  for (const auto& entry : std::filesystem::directory_iterator{keyframe_dir}) {
    if (entry.is_regular_file() && entry.path().extension() == ".jpg") {
      ++written_images;
    }
    CHECK(entry.path().string().find(".part.") == std::string::npos);
  }
  CHECK(written_images == image_count);

  std::filesystem::remove_all(base);
}

TEST_CASE("asynchronous writer admits one image larger than its memory budget") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto base = std::filesystem::temp_directory_path()
      / ("frame_extractor_oversized_writer_test_" + unique);
  std::filesystem::create_directories(base);
  const auto image_path = base / "oversized.png";
  const cv::Mat frame(12, 16, CV_8UC3, cv::Scalar{10, 20, 30});

  fe::detail::AsyncImageWriter writer{
      fe::ImageFormat::png,
      fe::detail::AsyncImageWriterSettings{
          .worker_count = 2U,
          .memory_budget_bytes = 1U,
          .job_limit = 1U}};
  writer.enqueue(image_path, frame);
  writer.finish();

  CHECK(writer.writtenCount() == 1U);
  CHECK(std::filesystem::is_regular_file(image_path));
  CHECK_FALSE(std::filesystem::exists(base / "oversized.part.png"));
  CHECK_THROWS_AS(writer.enqueue(image_path, frame), std::logic_error);
  CHECK_NOTHROW(writer.finish());

  std::filesystem::remove_all(base);
}

TEST_CASE("atomic image writer validates paths without damaging existing output") {
  const auto unique = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto base = std::filesystem::temp_directory_path()
      / ("frame_extractor_image_writer_test_" + unique);
  const cv::Mat frame(12, 16, CV_8UC3, cv::Scalar{10, 20, 30});
  const fe::detail::ImageFileWriter png_writer{fe::ImageFormat::png};

  CHECK_THROWS_AS(
      png_writer.writeAtomically(base / "nested" / "frame.jpg", frame),
      std::invalid_argument);
  CHECK_FALSE(std::filesystem::exists(base / "nested"));

  std::filesystem::create_directories(base);
  const auto existing = base / "frame.png";
  {
    std::ofstream output{existing, std::ios::binary};
    output << "existing";
  }
  CHECK_THROWS_AS(
      png_writer.writeAtomically(existing, {}),
      std::invalid_argument);
  CHECK(readText(existing) == "existing");
  CHECK_FALSE(std::filesystem::exists(base / "frame.part.png"));

  const auto uppercase = base / "frame.PNG";
  png_writer.writeAtomically(uppercase, frame);
  CHECK_FALSE(cv::imread(uppercase.string(), cv::IMREAD_COLOR).empty());

  std::filesystem::remove_all(base);
}
