#include "platform.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cctype>
#include <stdexcept>
#include <system_error>

namespace frame_extractor::gui {

std::filesystem::path pathFromUtf8(std::string_view path) {
  if (path.empty()) {
    return {};
  }
  const auto* begin = reinterpret_cast<const char8_t*>(path.data());
  return std::filesystem::path{std::u8string{begin, begin + path.size()}};
}

std::string pathToUtf8(const std::filesystem::path& path) {
  const auto encoded = path.u8string();
  if (encoded.empty()) {
    return {};
  }
  return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

std::filesystem::path applicationDataDirectory() {
  char* raw_path = SDL_GetPrefPath("DrMo", "FrameExtractor");
  if (raw_path == nullptr) {
    throw std::runtime_error(
        std::string{"Cannot locate application data: "} + SDL_GetError());
  }
  const std::filesystem::path path = pathFromUtf8(raw_path);
  SDL_free(raw_path);
  return path;
}

std::filesystem::path applicationResourceDirectory() {
  const char* const raw_directory = SDL_GetBasePath();
  if (raw_directory == nullptr) {
    throw std::runtime_error(
        std::string{"Cannot locate application resources: "} + SDL_GetError());
  }
  std::filesystem::path directory = pathFromUtf8(raw_directory);
#if defined(__APPLE__)
  directory /= "../Resources";
#endif
  return directory.lexically_normal();
}

std::string pathToFileUrl(const std::filesystem::path& path) {
  std::error_code error;
  const auto absolute = std::filesystem::absolute(path, error);
  const auto utf8 = (error ? path : absolute).lexically_normal().generic_u8string();

  std::string result{"file://"};
  if (utf8.empty() || utf8.front() != u8'/') {
    result += '/';
  }
  constexpr char hexadecimal[] = "0123456789ABCDEF";
  for (const char8_t raw_character : utf8) {
    const auto character = static_cast<unsigned char>(raw_character);
    if (std::isalnum(character) != 0 || character == '-' || character == '_'
        || character == '.' || character == '~' || character == '/'
        || character == ':') {
      result += static_cast<char>(character);
    } else {
      result += '%';
      result += hexadecimal[character >> 4U];
      result += hexadecimal[character & 0x0FU];
    }
  }
  return result;
}

bool openPath(const std::filesystem::path& path, std::string& error) {
  if (path.empty()) {
    error = "Cannot open an empty path";
    return false;
  }
  if (!SDL_OpenURL(pathToFileUrl(path).c_str())) {
    error = std::string{"Cannot open path: "} + SDL_GetError();
    return false;
  }
  error.clear();
  return true;
}

}  // namespace frame_extractor::gui
