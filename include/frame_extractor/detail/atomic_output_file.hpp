#pragma once

#include <filesystem>
#include <string_view>

namespace frame_extractor::detail {

class AtomicOutputFile final {
 public:
  explicit AtomicOutputFile(std::filesystem::path final_path);
  ~AtomicOutputFile();

  AtomicOutputFile(const AtomicOutputFile&) = delete;
  AtomicOutputFile& operator=(const AtomicOutputFile&) = delete;

  [[nodiscard]] const std::filesystem::path& temporaryPath() const noexcept;
  void publish();

 private:
  std::filesystem::path final_path_;
  std::filesystem::path temporary_path_;
  bool published_{};
};

void writeTextAtomically(
    const std::filesystem::path& path,
    std::string_view text);

}  // namespace frame_extractor::detail
