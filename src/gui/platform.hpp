#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace frame_extractor::gui {

[[nodiscard]] std::filesystem::path applicationDataDirectory();
[[nodiscard]] std::filesystem::path applicationResourceDirectory();
[[nodiscard]] std::filesystem::path pathFromUtf8(std::string_view path);
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);
[[nodiscard]] std::string pathToFileUrl(const std::filesystem::path& path);
[[nodiscard]] bool openPath(
    const std::filesystem::path& path,
    std::string& error);

}  // namespace frame_extractor::gui
