#include "fixture_support.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

namespace frame_extractor::test {

std::vector<std::uint8_t> decodeBase64(std::string_view encoded) {
  static constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::array<int, 256> lookup{};
  lookup.fill(-1);
  for (int index = 0; index < 64; ++index) {
    lookup[static_cast<unsigned char>(alphabet[static_cast<std::size_t>(index)])] = index;
  }

  std::vector<std::uint8_t> bytes;
  std::uint32_t accumulator = 0U;
  int bits = -8;
  for (const char raw_character : encoded) {
    const auto character = static_cast<unsigned char>(raw_character);
    if (std::isspace(character) != 0) {
      continue;
    }
    if (character == '=') {
      break;
    }
    const int value = lookup[character];
    if (value < 0) {
      throw std::runtime_error("invalid base64 fixture input");
    }
    accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(value);
    bits += 6;
    if (bits >= 0) {
      bytes.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return bytes;
}

void decodeBase64File(
    const std::filesystem::path& encoded_path,
    const std::filesystem::path& output_path) {
  std::ifstream input{encoded_path, std::ios::binary};
  if (!input.good()) {
    throw std::runtime_error("cannot open encoded fixture");
  }
  const std::string encoded{
      std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
  const auto bytes = decodeBase64(encoded);

  const auto parent = output_path.parent_path();
  if (!parent.empty()) {
    std::error_code error;
    std::filesystem::create_directories(parent, error);
    if (error) {
      throw std::runtime_error(
          "cannot create fixture output directory: " + error.message());
    }
  }
  std::ofstream output{output_path, std::ios::binary};
  output.write(
      reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  if (!output.good()) {
    throw std::runtime_error("cannot write decoded fixture");
  }
}

MaterializedFixture::MaterializedFixture(std::string fixture_name) {
  if (!fixture_name.ends_with(".b64")) {
    fixture_name += ".b64";
  }
  const auto encoded_path = std::filesystem::path{FRAME_EXTRACTOR_SOURCE_DIR}
      / "fixtures" / fixture_name;
  static std::atomic_uint64_t sequence{};
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  path_ = std::filesystem::temp_directory_path()
      / ("frame_extractor_" + std::to_string(stamp) + "_"
         + std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) + "_"
         + encoded_path.stem().filename().string());
  decodeBase64File(encoded_path, path_);
}

MaterializedFixture::~MaterializedFixture() {
  std::error_code error;
  std::filesystem::remove(path_, error);
}

}  // namespace frame_extractor::test
