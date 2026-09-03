#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct IconEntry {
  std::string_view type;
  std::string_view filename;
};

constexpr std::array entries{
    IconEntry{"ic04", "icon_16x16.png"},
    IconEntry{"ic11", "icon_16x16@2x.png"},
    IconEntry{"ic05", "icon_32x32.png"},
    IconEntry{"ic12", "icon_32x32@2x.png"},
    IconEntry{"ic07", "icon_128x128.png"},
    IconEntry{"ic13", "icon_128x128@2x.png"},
    IconEntry{"ic08", "icon_256x256.png"},
    IconEntry{"ic14", "icon_256x256@2x.png"},
    IconEntry{"ic09", "icon_512x512.png"},
    IconEntry{"ic10", "icon_512x512@2x.png"},
};

using Bytes = std::vector<std::uint8_t>;

Bytes readFile(const std::filesystem::path& path) {
  std::ifstream input{path, std::ios::binary | std::ios::ate};
  if (!input.good()) {
    throw std::runtime_error("cannot read icon representation: " + path.string());
  }

  const auto size = input.tellg();
  if (size <= 0) {
    throw std::runtime_error("empty icon representation: " + path.string());
  }
  Bytes bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(
      reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  if (!input.good()) {
    throw std::runtime_error("cannot read icon representation: " + path.string());
  }
  return bytes;
}

void writeBigEndian(std::ostream& output, std::uint32_t value) {
  const std::array bytes{
      static_cast<char>((value >> 24U) & 0xffU),
      static_cast<char>((value >> 16U) & 0xffU),
      static_cast<char>((value >> 8U) & 0xffU),
      static_cast<char>(value & 0xffU),
  };
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::uint32_t checkedSize(std::uint64_t value) {
  if (value > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error("generated icon exceeds the ICNS size limit");
  }
  return static_cast<std::uint32_t>(value);
}

void buildIcon(
    const std::filesystem::path& output_path,
    const std::filesystem::path& iconset_path) {
  std::array<Bytes, entries.size()> images;
  std::uint64_t total_size = 8U;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    images[index] = readFile(iconset_path / entries[index].filename);
    total_size += 8U + images[index].size();
  }

  auto temporary_path = output_path;
  temporary_path += ".part";
  std::error_code error;
  std::filesystem::remove(temporary_path, error);
  if (error) {
    throw std::runtime_error("cannot prepare icon output: " + error.message());
  }

  try {
    std::ofstream output{temporary_path, std::ios::binary};
    output.write("icns", 4);
    writeBigEndian(output, checkedSize(total_size));
    for (std::size_t index = 0; index < entries.size(); ++index) {
      output.write(entries[index].type.data(), 4);
      writeBigEndian(output, checkedSize(8U + images[index].size()));
      output.write(
          reinterpret_cast<const char*>(images[index].data()),
          static_cast<std::streamsize>(images[index].size()));
    }
    output.close();
    if (!output.good()) {
      throw std::runtime_error("cannot write icon output: " + output_path.string());
    }

    std::filesystem::rename(temporary_path, output_path);
  } catch (...) {
    std::filesystem::remove(temporary_path, error);
    throw;
  }
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "usage: macos-icns-builder OUTPUT.icns ICONSET_DIRECTORY\n";
    return 2;
  }

  try {
    buildIcon(argv[1], argv[2]);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "macos-icns-builder: " << error.what() << '\n';
    return 1;
  }
}
