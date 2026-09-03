#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace frame_extractor::test {

[[nodiscard]] std::vector<std::uint8_t> decodeBase64(std::string_view encoded);
void decodeBase64File(
    const std::filesystem::path& encoded_path,
    const std::filesystem::path& output_path);

class MaterializedFixture final {
 public:
  explicit MaterializedFixture(std::string fixture_name);
  ~MaterializedFixture();

  MaterializedFixture(const MaterializedFixture&) = delete;
  MaterializedFixture& operator=(const MaterializedFixture&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept {
    return path_;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace frame_extractor::test
