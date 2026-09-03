#include "run_files.hpp"

#include "frame_extractor/build_info.hpp"
#include "frame_extractor/detail/atomic_output_file.hpp"

#include <opencv2/core/version.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace frame_extractor::detail {
namespace {

std::string csvEscape(const std::string& value) {
  if (value.find_first_of(",\"\r\n") == std::string::npos) {
    return value;
  }
  std::string escaped{"\""};
  for (const char character : value) {
    if (character == '"') {
      escaped += "\"\"";
    } else {
      escaped += character;
    }
  }
  escaped += '"';
  return escaped;
}

std::string optionalInteger(std::optional<std::int64_t> value) {
  return value ? std::to_string(*value) : std::string{};
}

std::string formatDouble(double value) {
  std::array<char, 64> buffer{};
  const auto [end, error] = std::to_chars(
      buffer.data(), buffer.data() + buffer.size(), value);
  if (error != std::errc{}) {
    throw std::runtime_error("Cannot format floating-point output value");
  }
  return {buffer.data(), end};
}

std::string formatFixed(double value, int precision) {
  std::ostringstream output;
  output << std::fixed << std::setprecision(precision) << value;
  return output.str();
}

std::string groupDigits(std::string value) {
  const std::size_t first_digit = value.starts_with('-') ? 1U : 0U;
  std::string grouped;
  grouped.reserve(value.size() + value.size() / 3U);
  for (std::size_t index = 0U; index < value.size(); ++index) {
    if (index > first_digit && (value.size() - index) % 3U == 0U) {
      grouped += ',';
    }
    grouped += value[index];
  }
  return grouped;
}

std::string formatInteger(std::size_t value) {
  return groupDigits(std::to_string(value));
}

std::string formatInteger(std::int64_t value) {
  return groupDigits(std::to_string(value));
}

std::string formatOptionalInteger(std::optional<std::int64_t> value) {
  return value ? formatInteger(*value) : "Unavailable";
}

std::string formatDuration(double seconds) {
  if (std::abs(seconds) < 1.0) {
    return formatFixed(seconds * 1'000.0, 3) + " ms";
  }
  return formatFixed(seconds, 3) + " s";
}

std::string formatSeconds(double seconds) {
  return formatFixed(seconds, 3) + " s";
}

std::string formatTimestamp(double seconds) {
  const bool negative = seconds < 0.0;
  const auto milliseconds = std::llround(std::abs(seconds) * 1'000.0);
  const auto hours = milliseconds / 3'600'000;
  const auto minutes = (milliseconds / 60'000) % 60;
  const auto whole_seconds = (milliseconds / 1'000) % 60;
  const auto remaining_milliseconds = milliseconds % 1'000;

  std::ostringstream output;
  if (negative) {
    output << '-';
  }
  output << std::setfill('0');
  if (hours > 0) {
    output << hours << ':';
  }
  output << std::setw(2) << minutes << ':'
         << std::setw(2) << whole_seconds << '.'
         << std::setw(3) << remaining_milliseconds;
  return output.str();
}

std::string humanize(std::string value) {
  if (value.empty()) {
    return "Custom";
  }
  bool capitalize_next = true;
  for (char& character : value) {
    if (character == '_' || character == '-') {
      character = ' ';
      capitalize_next = true;
    } else if (capitalize_next) {
      character = static_cast<char>(
          std::toupper(static_cast<unsigned char>(character)));
      capitalize_next = false;
    }
  }
  return value;
}

std::string ordinal(std::size_t value) {
  std::string suffix = "th";
  const std::size_t last_two_digits = value % 100U;
  if (last_two_digits < 11U || last_two_digits > 13U) {
    switch (value % 10U) {
      case 1U:
        suffix = "st";
        break;
      case 2U:
        suffix = "nd";
        break;
      case 3U:
        suffix = "rd";
        break;
      default:
        break;
    }
  }
  return formatInteger(value) + suffix;
}

std::string yesNo(bool value) {
  return value ? "Yes" : "No";
}

std::string formatAverageSpacing(
    std::size_t processed_frames,
    std::size_t keyframes) {
  if (keyframes == 0U) {
    return "Unavailable";
  }
  return formatFixed(
             static_cast<double>(processed_frames) / static_cast<double>(keyframes),
             2)
      + " frames per keyframe";
}

std::string formatPercentage(std::size_t numerator, std::size_t denominator) {
  if (denominator == 0U) {
    return "Unavailable";
  }
  return formatFixed(
             100.0 * static_cast<double>(numerator)
                 / static_cast<double>(denominator),
             2)
      + '%';
}

std::string formatDurationWithShare(double seconds, double total_seconds) {
  std::string formatted = formatDuration(seconds);
  if (total_seconds > 0.0) {
    formatted += " (" + formatFixed(100.0 * seconds / total_seconds, 2) + "%)";
  }
  return formatted;
}

class SummaryReport {
 public:
  explicit SummaryReport(std::string_view title) {
    heading(title, '=');
  }

  void section(std::string_view title) {
    output_ << '\n';
    heading(title, '-');
  }

  void field(
      std::string_view label,
      std::string_view value,
      std::size_t indentation = 0U) {
    std::string display_label(indentation, ' ');
    display_label += label;
    display_label += ':';
    output_ << std::left << std::setw(value_column_) << display_label << value << '\n';
  }

  void line(std::string_view value) {
    output_ << value << '\n';
  }

  void blankLine() {
    output_ << '\n';
  }

  [[nodiscard]] std::string str() const {
    return output_.str();
  }

 private:
  void heading(std::string_view title, char underline) {
    output_ << title << '\n' << std::string(title.size(), underline) << '\n';
  }

  static constexpr int value_column_ = 26;
  std::ostringstream output_;
};

std::string imageFilename(const SelectedFrame& selected, ImageFormat format) {
  std::ostringstream name;
  name << "keyframe_" << std::setfill('0') << std::setw(4) << selected.keyframe_index
       << '_' << std::setw(6) << selected.decoded_frame_index << '.' << toString(format);
  return name.str();
}

std::string regionDirectoryName(std::size_t region_index) {
  std::ostringstream name;
  name << "region_" << std::setfill('0') << std::setw(2) << region_index + 1U;
  return name.str();
}

std::string buildManifest(
    const ProcessingResult& result,
    ImageFormat image_format,
    bool group_keyframes_by_region) {
  std::ostringstream manifest;
  manifest << "filename,keyframe_index,source_frame_index,region_index,pts,pos_seconds_raw,"
              "timing_status,selection_reason,motion_score_px,in_bounds_ratio\n";
  for (const auto& selected : result.selected_frames) {
    const auto relative = keyframeRelativePath(
        selected, image_format, group_keyframes_by_region).generic_string();
    manifest << csvEscape(relative) << ',' << selected.keyframe_index << ','
             << selected.decoded_frame_index << ',' << selected.region_index << ','
             << optionalInteger(selected.pts) << ',';
    if (const auto seconds = selected.ptsSeconds()) {
      manifest << formatDouble(*seconds);
    }
    manifest << ',' << selected.timing_status << ',' << csvEscape(selected.selection_reason)
             << ',' << formatDouble(selected.scores.global_score) << ','
             << formatDouble(selected.scores.in_bounds_ratio) << '\n';
  }
  return manifest.str();
}

std::string buildSummary(
    const std::filesystem::path& input_video,
    const ProcessOptions& process_options,
    const VideoInfo& video_info,
    const ProcessingResult& result,
    std::chrono::duration<double> runtime,
    ImageFormat image_format,
    bool group_keyframes_by_region,
    const ImageEncodingSettings& encoding_settings,
    std::string_view failure_message) {
  const double runtime_seconds = runtime.count();
  const auto& timings = result.timings;
  const auto normalized_regions = normalizeTimeRanges(process_options.regions);
  const double profiled_pipeline_seconds =
      timings.source_read_seconds
      + timings.analysis_preparation_seconds
      + timings.dense_flow_seconds
      + timings.point_sampling_seconds
      + timings.scoring_seconds
      + timings.keyframe_sink_seconds
      + timings.preview_sink_seconds;
  const double unattributed_seconds = std::max(
      0.0, runtime_seconds - profiled_pipeline_seconds);
  const double fps = video_info.framesPerSecond().value_or(0.0);
  const bool timing_valid = result.pts_unavailable_frames == 0U
      && result.pts_non_monotonic_frames == 0U;
  double requested_region_duration = 0.0;
  for (const auto& region : normalized_regions) {
    requested_region_duration += region.end_seconds - region.start_seconds;
  }

  SummaryReport summary{"Frame Extractor - Run Summary"};

  summary.section("Result");
  summary.field(
      "Status",
      !failure_message.empty() ? "Failed"
          : result.cancelled ? "Cancelled"
                             : "Completed");
  if (!failure_message.empty()) {
    summary.field("Error", failure_message);
  }
  summary.field("Frames processed", formatInteger(result.processed_frames));
  summary.field("Keyframes saved", formatInteger(result.selected_frames.size()));
  summary.field("Trigger events", formatInteger(result.trigger_count));
  summary.field(
      "Average spacing",
      formatAverageSpacing(result.processed_frames, result.selected_frames.size()));
  summary.field(
      "Selection rate",
      formatPercentage(result.selected_frames.size(), result.processed_frames));
  if (!normalized_regions.empty()) {
    summary.field(
        "Regions processed",
        formatInteger(result.regions_processed) + " of "
            + formatInteger(normalized_regions.size()));
  }

  summary.section("Input");
  summary.field("Video", input_video.string());
  summary.field(
      "Scope",
      normalized_regions.empty() ? "Entire video" : "Selected regions");
  if (!normalized_regions.empty()) {
    summary.field("Selected duration", formatSeconds(requested_region_duration));
  }
  if (process_options.start_frame > 0U) {
    summary.field("Starting frame", formatInteger(process_options.start_frame));
  }
  if (process_options.max_frames) {
    summary.field("Maximum frames", formatInteger(*process_options.max_frames));
  }

  summary.section("Extraction Settings");
  if (process_options.fixed_frame_interval) {
    summary.field(
        "Selection",
        "Fixed interval (every "
            + ordinal(*process_options.fixed_frame_interval) + " frame)");
  } else {
    summary.field(
        "Selection",
        "Adaptive (" + humanize(process_options.selection_profile) + " profile)");
  }
  if (image_format == ImageFormat::jpg) {
    summary.field(
        "Image format",
        "JPEG, quality " + std::to_string(encoding_settings.jpeg_quality));
  } else {
    summary.field(
        "Image format",
        "PNG, compression level "
            + std::to_string(encoding_settings.png_compression_level));
  }
  summary.field(
      "Keyframe layout",
      group_keyframes_by_region ? "One directory per region" : "Single directory");
  summary.field(
      "Manifest",
      "keyframes.csv (schema " + std::to_string(keyframesCsvSchemaVersion) + ')');

  if (!normalized_regions.empty()) {
    summary.section("Regions");
    bool first_region = true;
    for (const auto& region : result.processed_regions) {
      if (!first_region) {
        summary.blankLine();
      }
      first_region = false;
      summary.line("Region " + formatInteger(region.region_index + 1U));
      summary.field(
          "Time",
          formatTimestamp(region.first_timestamp_seconds) + " - "
              + formatTimestamp(region.last_timestamp_seconds) + " ("
              + formatSeconds(
                  std::max(
                      0.0,
                      region.last_timestamp_seconds - region.first_timestamp_seconds))
              + ')',
          2U);
      summary.field(
          "Source frames",
          formatInteger(region.first_decoded_frame_index) + " - "
              + formatInteger(region.last_decoded_frame_index),
          2U);
      summary.field(
          "Frames processed", formatInteger(region.processed_frames), 2U);
      summary.field("Keyframes saved", formatInteger(region.keyframes), 2U);
      summary.field(
          "Average spacing",
          formatAverageSpacing(region.processed_frames, region.keyframes),
          2U);
    }
  }

  summary.section("Performance");
  summary.field("Runtime", formatSeconds(runtime_seconds));
  summary.field(
      "Processing throughput",
      runtime_seconds > 0.0
          ? formatFixed(
                static_cast<double>(result.processed_frames) / runtime_seconds,
                2)
                + " frames/s"
          : "Unavailable");
  summary.field(
      "Accounted time",
      formatDurationWithShare(profiled_pipeline_seconds, runtime_seconds));
  summary.field(
      "Other time",
      formatDurationWithShare(unattributed_seconds, runtime_seconds));

  summary.section("Pipeline Timing");
  summary.field("Source reading", formatDuration(timings.source_read_seconds));
  summary.field(
      "Analysis preparation",
      formatDuration(timings.analysis_preparation_seconds));
  summary.field("Dense optical flow", formatDuration(timings.dense_flow_seconds));
  summary.field("Point sampling", formatDuration(timings.point_sampling_seconds));
  summary.field("Scoring", formatDuration(timings.scoring_seconds));
  summary.field("Keyframe writing", formatDuration(timings.keyframe_sink_seconds));
  summary.field("Preview delivery", formatDuration(timings.preview_sink_seconds));

  summary.section("Source Reading Breakdown");
  summary.field("Packet decoding", formatDuration(timings.packet_decode_seconds));
  summary.field(
      "Hardware transfer", formatDuration(timings.hardware_transfer_seconds));
  summary.field("Pixel conversion", formatDuration(timings.pixel_conversion_seconds));
  summary.field("Rotation", formatDuration(timings.rotation_seconds));

  summary.section("Decoder and Analysis");
  summary.field("Backend", "libav");
  summary.field(
      "Codec", video_info.codec_name.empty() ? "Unavailable" : video_info.codec_name);
  summary.field("Hardware decoding", yesNo(video_info.hardware_accelerated_decode));
  summary.field("Source frames read", formatInteger(timings.source_frames_read));
  summary.field(
      "Native-luma frames",
      formatInteger(timings.analysis_native_luma_frames));
  summary.field(
      "FFmpeg fallback frames",
      formatInteger(timings.analysis_ffmpeg_fallback_frames));

  summary.section("Video Details");
  summary.field(
      "Frame rate",
      fps > 0.0 ? formatFixed(fps, 2) + " fps" : "Unavailable");
  summary.field(
      "Reported frame count",
      formatOptionalInteger(video_info.reported_frame_count));
  summary.field(
      "Duration",
      video_info.duration_seconds
          ? formatSeconds(*video_info.duration_seconds)
          : "Unavailable");
  summary.field(
      "Start time",
      video_info.start_time_seconds
          ? formatSeconds(*video_info.start_time_seconds)
          : "Unavailable");
  summary.field("Rotation", std::to_string(video_info.rotation_degrees) + " degrees");
  summary.field(
      "PTS time base",
      std::to_string(video_info.time_base.numerator) + '/'
          + std::to_string(video_info.time_base.denominator));

  summary.section("Timing Validation");
  summary.field("Status", timing_valid ? "OK" : "Warnings");
  summary.field("PTS available", formatInteger(result.pts_available_frames));
  summary.field("PTS unavailable", formatInteger(result.pts_unavailable_frames));
  summary.field(
      "Non-monotonic PTS",
      formatInteger(result.pts_non_monotonic_frames));
  summary.field(
      "Exact indices on seek",
      yesNo(video_info.exact_frame_indices_after_seek));

  summary.section("Software");
  summary.field("Version", build::version);
  summary.field("Build", build::id);
  summary.field("OpenCV", CV_VERSION);
  return summary.str();
}

}  // namespace

std::filesystem::path keyframeRelativePath(
    const SelectedFrame& selected,
    ImageFormat format,
    bool group_by_region) {
  std::filesystem::path path{"keyframes"};
  if (group_by_region) {
    path /= regionDirectoryName(selected.region_index);
  }
  return path / imageFilename(selected, format);
}

void writeConfigSnapshot(const RunPaths& paths, const Config& config) {
  writeTextAtomically(paths.config_path, dumpConfigYaml(config));
}

void writeRunReports(
    const RunPaths& paths,
    const std::filesystem::path& input_video,
    const ProcessOptions& process_options,
    const VideoInfo& video_info,
    const ProcessingResult& result,
    std::chrono::duration<double> runtime,
    ImageFormat image_format,
    bool group_keyframes_by_region,
    const ImageEncodingSettings& encoding_settings,
    std::string_view failure_message) {
  const auto manifest = buildManifest(
      result, image_format, group_keyframes_by_region);
  const auto summary = buildSummary(
      input_video,
      process_options,
      video_info,
      result,
      runtime,
      image_format,
      group_keyframes_by_region,
      encoding_settings,
      failure_message);
  writeTextAtomically(paths.manifest_path, manifest);
  writeTextAtomically(paths.summary_path, summary);
}

}  // namespace frame_extractor::detail
