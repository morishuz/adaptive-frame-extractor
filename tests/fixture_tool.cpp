#include "fixture_support.hpp"

#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
  try {
    if (argc != 3) {
      throw std::invalid_argument("usage: fixture-tool INPUT.b64 OUTPUT");
    }
    frame_extractor::test::decodeBase64File(argv[1], argv[2]);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "fixture-tool: " << error.what() << '\n';
    return 1;
  }
}
