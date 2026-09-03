#include "frame_extractor/detail/atomic_output_file.hpp"

#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace frame_extractor::detail {

AtomicOutputFile::AtomicOutputFile(std::filesystem::path final_path)
    : final_path_{std::move(final_path)}, temporary_path_{final_path_} {
  auto temporary_name = final_path_.stem();
  temporary_name += ".part";
  temporary_name += final_path_.extension();
  temporary_path_.replace_filename(std::move(temporary_name));

  std::error_code error;
  std::filesystem::remove(temporary_path_, error);
  if (error) {
    throw std::runtime_error(
        "Cannot prepare temporary output file: " + error.message());
  }
}

AtomicOutputFile::~AtomicOutputFile() {
  if (!published_) {
    std::error_code error;
    std::filesystem::remove(temporary_path_, error);
  }
}

const std::filesystem::path& AtomicOutputFile::temporaryPath() const noexcept {
  return temporary_path_;
}

void AtomicOutputFile::publish() {
#if defined(_WIN32)
  if (!MoveFileExW(
          temporary_path_.c_str(),
          final_path_.c_str(),
          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw std::system_error(
        static_cast<int>(GetLastError()),
        std::system_category(),
        "Cannot publish output file " + final_path_.string());
  }
#else
  std::error_code error;
  std::filesystem::rename(temporary_path_, final_path_, error);
  if (error) {
    throw std::runtime_error(
        "Cannot publish output file " + final_path_.string() + ": "
        + error.message());
  }
#endif
  published_ = true;
}

void writeTextAtomically(
    const std::filesystem::path& path,
    std::string_view text) {
  AtomicOutputFile output_file{path};
  std::ofstream output{output_file.temporaryPath(), std::ios::binary};
  if (!output.good()) {
    throw std::runtime_error("Cannot write file: " + path.string());
  }
  output.write(text.data(), static_cast<std::streamsize>(text.size()));
  output.close();
  if (!output.good()) {
    throw std::runtime_error("Failed while writing file: " + path.string());
  }
  output_file.publish();
}

}  // namespace frame_extractor::detail
