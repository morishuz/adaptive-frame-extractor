#pragma once

#include "async_image_writer.hpp"
#include "frame_extractor/output.hpp"

#include <chrono>
#include <filesystem>
#include <string_view>

namespace frame_extractor::detail {

[[nodiscard]] std::filesystem::path keyframeRelativePath(
    const SelectedFrame& selected,
    ImageFormat format,
    bool group_by_region);

void writeConfigSnapshot(const RunPaths& paths, const Config& config);

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
    std::string_view failure_message = {});

}  // namespace frame_extractor::detail
