#include "frame_extractor/build_info.hpp"
#include "frame_extractor/config.hpp"
#include "frame_extractor/diagnostics.hpp"
#include "frame_extractor/extraction_run.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>

namespace fe = frame_extractor;
namespace {

struct CliOptions {
  std::filesystem::path input_video;
  std::optional<std::filesystem::path> config_path;
  std::optional<std::filesystem::path> output_dir;
  std::size_t start_frame{};
  std::optional<std::size_t> max_frames;
};

std::atomic<fe::CancellationToken*> active_token{nullptr};
static_assert(decltype(active_token)::is_always_lock_free);

extern "C" void handleInterrupt(int) {
  if (auto* const token = active_token.load(std::memory_order_relaxed);
      token != nullptr) {
    token->requestCancellation();
  }
}

class InterruptHandlerGuard final {
 public:
  explicit InterruptHandlerGuard(fe::CancellationToken& token) {
    active_token.store(&token, std::memory_order_relaxed);
    previous_handler_ = std::signal(SIGINT, handleInterrupt);
    if (previous_handler_ == SIG_ERR) {
      active_token.store(nullptr, std::memory_order_relaxed);
      throw std::runtime_error("cannot install interrupt handler");
    }
  }

  ~InterruptHandlerGuard() {
    active_token.store(nullptr, std::memory_order_relaxed);
    std::signal(SIGINT, previous_handler_);
  }

  InterruptHandlerGuard(const InterruptHandlerGuard&) = delete;
  InterruptHandlerGuard& operator=(const InterruptHandlerGuard&) = delete;

 private:
  using Handler = void (*)(int);
  Handler previous_handler_{SIG_DFL};
};

void printUsage(std::ostream& output) {
  output
      << "Usage: frame-extractor INPUT_VIDEO [options]\n\n"
      << "Extract motion-based keyframes with native libav decoding and OpenCV DIS flow.\n\n"
      << "Options:\n"
      << "  --config PATH           YAML configuration (built-in defaults when omitted)\n"
      << "  --output-dir PATH       Base directory for keyframes, CSV, and summary\n"
      << "  --start-frame N         First presentation-frame index (default: 0)\n"
      << "  --max-frames N          Maximum frames to process (must be >= 1)\n"
      << "  --duration-frames N     Alias for --max-frames\n"
      << "  -h, --help              Show this help\n"
      << "  --version               Show the version\n";
}

std::size_t parseNonnegative(std::string_view text, std::string_view option, bool allow_zero) {
  std::size_t value{};
  const auto* begin = text.data();
  const auto* end = begin + text.size();
  const auto [position, error] = std::from_chars(begin, end, value);
  if (error != std::errc{} || position != end || (!allow_zero && value == 0U)) {
    throw std::invalid_argument(
        std::string{option} + (allow_zero ? " must be an integer >= 0" : " must be an integer >= 1"));
  }
  return value;
}

std::string_view requireValue(int& index, int argc, char** argv, std::string_view option) {
  if (index + 1 >= argc) {
    throw std::invalid_argument(std::string{option} + " requires a value");
  }
  ++index;
  return argv[index];
}

CliOptions parseArguments(int argc, char** argv) {
  CliOptions options;
  std::optional<std::size_t> duration_frames;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument{argv[index]};
    if (argument == "--config") {
      options.config_path = requireValue(index, argc, argv, argument);
    } else if (argument == "--output-dir") {
      options.output_dir = requireValue(index, argc, argv, argument);
    } else if (argument == "--start-frame") {
      options.start_frame = parseNonnegative(
          requireValue(index, argc, argv, argument), argument, true);
    } else if (argument == "--max-frames") {
      options.max_frames = parseNonnegative(
          requireValue(index, argc, argv, argument), argument, false);
    } else if (argument == "--duration-frames") {
      duration_frames = parseNonnegative(
          requireValue(index, argc, argv, argument), argument, false);
    } else if (!argument.empty() && argument.front() == '-') {
      throw std::invalid_argument("unknown option: " + std::string{argument});
    } else if (options.input_video.empty()) {
      options.input_video = argument;
    } else {
      throw std::invalid_argument("only one input video may be provided");
    }
  }
  if (options.input_video.empty()) {
    throw std::invalid_argument("an input video is required");
  }
  if (duration_frames && options.max_frames && *duration_frames != *options.max_frames) {
    throw std::invalid_argument(
        "--max-frames and --duration-frames must match when both are provided");
  }
  if (duration_frames) {
    options.max_frames = duration_frames;
  }
  return options;
}

class TerminalObserver final : public fe::DiagnosticObserver {
 public:
  void onEvent(const fe::DiagnosticEvent& event) override {
    if (const auto* started = std::get_if<fe::RunStartedEvent>(&event)) {
      std::cerr << "Processing " << started->input_path;
      if (started->total_frames) {
        std::cerr << " (" << *started->total_frames << " frames)";
      }
      std::cerr << "\n";
    } else if (const auto* frame = std::get_if<fe::FrameAnalyzedEvent>(&event)) {
      if (frame->processed_index == 0U || frame->trigger.triggered
          || (frame->processed_index + 1U) % 30U == 0U) {
        std::cerr << "frame " << frame->scores.frame_index
                  << "  motion=" << frame->scores.global_score
                  << "  in_bounds=" << frame->scores.in_bounds_ratio;
        if (frame->trigger.triggered) {
          std::cerr << "  trigger=" << frame->trigger.displayReason();
        }
        std::cerr << '\n';
      }
    } else if (const auto* selected = std::get_if<fe::KeyframeSelectedEvent>(&event)) {
      std::cerr << "selected keyframe " << selected->keyframe_index << " from frame "
                << selected->decoded_frame_index << " (" << selected->selection_reason << ")\n";
    } else if (const auto* warning = std::get_if<fe::WarningEvent>(&event)) {
      std::cerr << "warning: " << warning->message << '\n';
    } else if (const auto* finished = std::get_if<fe::RunFinishedEvent>(&event)) {
      std::cerr << "Finished: " << finished->processed_frames << " frames, "
                << finished->selected_keyframes << " keyframes"
                << (finished->cancelled ? " (cancelled)" : "") << "\n";
    }
  }
};

void printProfile(
    std::ostream& output,
    const fe::ProcessingTimings& timings,
    std::chrono::duration<double> runtime) {
  const double total = std::max(runtime.count(), 1.0e-9);
  const auto stage = [&](std::string_view name, double seconds) {
    output << "  " << name << ": " << seconds << " s ("
           << 100.0 * seconds / total << "%)\n";
  };
  const double accounted = timings.source_read_seconds
      + timings.analysis_preparation_seconds
      + timings.dense_flow_seconds
      + timings.point_sampling_seconds
      + timings.scoring_seconds
      + timings.keyframe_sink_seconds
      + timings.preview_sink_seconds;

  output << "Performance profile (" << timings.source_frames_read << " decoded frames):\n";
  output << "  analysis conversion: "
         << timings.analysis_native_luma_frames << " native luma, "
         << timings.analysis_ffmpeg_fallback_frames << " FFmpeg fallback\n";
  stage("source read", timings.source_read_seconds);
  stage("  packet decode", timings.packet_decode_seconds);
  stage("  hardware transfer", timings.hardware_transfer_seconds);
  stage("  pixel conversion/scale", timings.pixel_conversion_seconds);
  stage("  rotation", timings.rotation_seconds);
  stage("analysis resize + grayscale", timings.analysis_preparation_seconds);
  stage("dense DIS flow", timings.dense_flow_seconds);
  stage("point sampling", timings.point_sampling_seconds);
  stage("scoring", timings.scoring_seconds);
  stage("keyframe sink", timings.keyframe_sink_seconds);
  stage("preview sink", timings.preview_sink_seconds);
  stage("unattributed", std::max(0.0, total - accounted));
}

}  // namespace

int main(int argc, char** argv) {
  try {
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument{argv[index]};
      if (argument == "-h" || argument == "--help") {
        printUsage(std::cout);
        return 0;
      }
      if (argument == "--version") {
        std::cout << "frame-extractor " << fe::build::display << '\n';
        return 0;
      }
    }

    const auto options = parseArguments(argc, argv);
    const auto config = options.config_path ? fe::loadConfig(*options.config_path) : fe::Config{};
    fe::CancellationToken cancellation;
    TerminalObserver observer;
    const fe::ProcessOptions process_options{
        options.input_video.string(), options.start_frame, options.max_frames};
    const auto result = [&] {
      InterruptHandlerGuard interrupt_handler{cancellation};
      return fe::runExtraction(
          fe::ExtractionRunRequest{
              options.input_video,
              options.output_dir,
              config,
              process_options,
              {}},
          &observer,
          &cancellation,
          nullptr,
          nullptr,
          [](const fe::RunPaths& paths) {
            std::cerr << "Writing output to " << paths.run_dir.string() << '\n';
          });
    }();
    printProfile(std::cerr, result.processing.timings, result.runtime);

    if (result.output_paths && result.outputs_finalized) {
      std::cout << result.output_paths->run_dir.string() << '\n';
    }
    if (result.status == fe::ExtractionRunStatus::failed) {
      std::cerr << "error: " << result.error << '\n';
      return 2;
    }
    return result.status == fe::ExtractionRunStatus::cancelled ? 130 : 0;
  } catch (const std::exception& error) {
    active_token.store(nullptr, std::memory_order_relaxed);
    std::cerr << "error: " << error.what() << "\n\n";
    printUsage(std::cerr);
    return 2;
  }
}
