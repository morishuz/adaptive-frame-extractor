#include "frame_extractor/decoder.hpp"

#include "frame_extractor/image_processing.hpp"
#include "video_timing.hpp"

#include <opencv2/imgproc.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace frame_extractor {
namespace {

using Clock = std::chrono::steady_clock;

double elapsedSeconds(Clock::time_point start) {
  return std::chrono::duration<double>(Clock::now() - start).count();
}

std::string utf8Path(const std::filesystem::path& path) {
  const auto encoded = path.u8string();
  if (encoded.empty()) {
    return {};
  }
  return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

struct SourcePixelFormat {
  AVPixelFormat format{AV_PIX_FMT_NONE};
  bool full_range{};
};

SourcePixelFormat sourcePixelFormat(const AVFrame& frame) {
  SourcePixelFormat source{
      static_cast<AVPixelFormat>(frame.format),
      frame.color_range == AVCOL_RANGE_JPEG};
  switch (source.format) {
    case AV_PIX_FMT_YUVJ420P:
      source.format = AV_PIX_FMT_YUV420P;
      source.full_range = true;
      break;
    case AV_PIX_FMT_YUVJ411P:
      source.format = AV_PIX_FMT_YUV411P;
      source.full_range = true;
      break;
    case AV_PIX_FMT_YUVJ422P:
      source.format = AV_PIX_FMT_YUV422P;
      source.full_range = true;
      break;
    case AV_PIX_FMT_YUVJ444P:
      source.format = AV_PIX_FMT_YUV444P;
      source.full_range = true;
      break;
    case AV_PIX_FMT_YUVJ440P:
      source.format = AV_PIX_FMT_YUV440P;
      source.full_range = true;
      break;
    default:
      break;
  }
  return source;
}

void configureColorspace(
    SwsContext& context,
    const AVFrame& frame,
    bool source_full_range) {
  const int colorspace = frame.colorspace == AVCOL_SPC_UNSPECIFIED
      ? SWS_CS_DEFAULT
      : static_cast<int>(frame.colorspace);
  const int* coefficients = sws_getCoefficients(colorspace);
  if (coefficients == nullptr) {
    return;
  }
  static_cast<void>(sws_setColorspaceDetails(
      &context,
      coefficients,
      source_full_range ? 1 : 0,
      coefficients,
      1,
      0,
      1 << 16,
      1 << 16));
}

cv::Mat convertToBgr(
    const AVFrame& frame,
    cv::Size destination_size,
    int scaling_flags,
    SwsContext*& context) {
  const auto source_pixel_format = sourcePixelFormat(frame);
  context = sws_getCachedContext(
      context,
      frame.width,
      frame.height,
      source_pixel_format.format,
      destination_size.width,
      destination_size.height,
      AV_PIX_FMT_BGR24,
      scaling_flags,
      nullptr,
      nullptr,
      nullptr);
  if (context == nullptr) {
    throw std::runtime_error("Cannot create video color-conversion context");
  }
  configureColorspace(*context, frame, source_pixel_format.full_range);

  cv::Mat bgr(destination_size, CV_8UC3);
  std::array<std::uint8_t*, 4> destination_data{bgr.data, nullptr, nullptr, nullptr};
  std::array<int, 4> destination_linesize{static_cast<int>(bgr.step[0]), 0, 0, 0};
  const int converted_rows = sws_scale(
      context,
      frame.data,
      frame.linesize,
      0,
      frame.height,
      destination_data.data(),
      destination_linesize.data());
  if (converted_rows != destination_size.height) {
    throw std::runtime_error("Video color conversion returned an incomplete frame");
  }
  return bgr;
}

cv::Mat convertToGray(
    const AVFrame& frame,
    cv::Size destination_size,
    SwsContext*& context) {
  const auto source_pixel_format = sourcePixelFormat(frame);
  context = sws_getCachedContext(
      context,
      frame.width,
      frame.height,
      source_pixel_format.format,
      destination_size.width,
      destination_size.height,
      AV_PIX_FMT_GRAY8,
      SWS_AREA,
      nullptr,
      nullptr,
      nullptr);
  if (context == nullptr) {
    throw std::runtime_error("Cannot create grayscale analysis conversion context");
  }
  configureColorspace(*context, frame, source_pixel_format.full_range);

  cv::Mat gray(destination_size, CV_8UC1);
  std::array<std::uint8_t*, 4> destination_data{gray.data, nullptr, nullptr, nullptr};
  std::array<int, 4> destination_linesize{static_cast<int>(gray.step[0]), 0, 0, 0};
  const int converted_rows = sws_scale(
      context,
      frame.data,
      frame.linesize,
      0,
      frame.height,
      destination_data.data(),
      destination_linesize.data());
  if (converted_rows != destination_size.height) {
    throw std::runtime_error("Grayscale analysis conversion returned an incomplete frame");
  }
  return gray;
}

std::optional<cv::Mat> convertNativeLumaWithOpenCv(
    const AVFrame& frame,
    cv::Size destination_size) {
  const auto format = static_cast<AVPixelFormat>(frame.format);
  const AVPixFmtDescriptor* descriptor = av_pix_fmt_desc_get(format);
  constexpr std::uint64_t unsupported_flags =
      AV_PIX_FMT_FLAG_BE | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM
      | AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_BAYER
      | AV_PIX_FMT_FLAG_FLOAT | AV_PIX_FMT_FLAG_XYZ;
  if (descriptor == nullptr || descriptor->nb_components < 1
      || (descriptor->flags & unsupported_flags) != 0) {
    return std::nullopt;
  }

  const AVComponentDescriptor& luma = descriptor->comp[0];
  if (luma.plane >= 4 || frame.data[luma.plane] == nullptr
      || frame.linesize[luma.plane] <= 0 || luma.offset != 0) {
    return std::nullopt;
  }

  // OpenCV can only view the luma plane directly when each addressable sample
  // contains luma and nothing else. Packed YUV (for example YUYV422) and
  // interleaved gray/alpha formats also have offset-zero luma, but their step
  // describes a whole packed pixel rather than a 16-bit luma sample.
  if (descriptor->nb_components > 1
      && (descriptor->flags & AV_PIX_FMT_FLAG_PLANAR) == 0) {
    return std::nullopt;
  }
  for (int component = 1; component < descriptor->nb_components; ++component) {
    if (descriptor->comp[component].plane == luma.plane) {
      return std::nullopt;
    }
  }

  int source_type = 0;
  int storage_bits = 0;
  if (luma.depth == 8 && luma.step == 1) {
    source_type = CV_8UC1;
    storage_bits = 8;
  } else if (luma.depth > 8 && luma.depth <= 16 && luma.step == 2) {
    source_type = CV_16UC1;
    storage_bits = 16;
  } else {
    return std::nullopt;
  }
  if (luma.shift < 0 || luma.shift + luma.depth > storage_bits
      || static_cast<std::int64_t>(frame.linesize[luma.plane])
          < static_cast<std::int64_t>(frame.width) * luma.step) {
    return std::nullopt;
  }

  const cv::Mat source{
      frame.height,
      frame.width,
      source_type,
      frame.data[luma.plane],
      static_cast<std::size_t>(frame.linesize[luma.plane])};
  cv::Mat resized;
  cv::resize(source, resized, destination_size, 0.0, 0.0, cv::INTER_AREA);

  const bool full_range = sourcePixelFormat(frame).full_range;
  const int storage_shift = luma.shift;
  if (source_type == CV_8UC1 && full_range && storage_shift == 0) {
    return resized;
  }
  const int coding_shift = std::max(0, luma.depth - 8);
  const double minimum = full_range
      ? 0.0
      : static_cast<double>((16 << coding_shift) << storage_shift);
  const double maximum = static_cast<double>(
      (full_range ? ((1 << luma.depth) - 1) : (235 << coding_shift))
      << storage_shift);
  if (maximum <= minimum) {
    return std::nullopt;
  }

  cv::Mat gray;
  const double scale = 255.0 / (maximum - minimum);
  resized.convertTo(gray, CV_8UC1, scale, -minimum * scale);
  return gray;
}

cv::Mat applyDisplayRotation(cv::Mat image, int rotation_degrees) {
  if (rotation_degrees == 0) {
    return image;
  }
  cv::Mat oriented;
  switch (rotation_degrees) {
    case 90:
      cv::rotate(image, oriented, cv::ROTATE_90_COUNTERCLOCKWISE);
      break;
    case 180:
      cv::rotate(image, oriented, cv::ROTATE_180);
      break;
    case 270:
      cv::rotate(image, oriented, cv::ROTATE_90_CLOCKWISE);
      break;
    default:
      throw std::logic_error("unexpected display rotation");
  }
  return oriented;
}

cv::Mat convertToBgrOnce(
    const AVFrame& frame,
    cv::Size destination_size,
    int scaling_flags) {
  SwsContext* context = nullptr;
  try {
    auto bgr = convertToBgr(
        frame, destination_size, scaling_flags, context);
    sws_freeContext(context);
    return bgr;
  } catch (...) {
    sws_freeContext(context);
    throw;
  }
}

std::shared_ptr<AVFrame> cloneFrame(const AVFrame& frame) {
  AVFrame* cloned = av_frame_clone(&frame);
  if (cloned == nullptr) {
    throw std::bad_alloc{};
  }
  return std::shared_ptr<AVFrame>{cloned, [](AVFrame* value) { av_frame_free(&value); }};
}

std::function<cv::Mat()> fullBgrRenderer(
    std::shared_ptr<AVFrame> frame,
    int rotation_degrees) {
  return [frame = std::move(frame), rotation_degrees] {
    return applyDisplayRotation(
        convertToBgrOnce(
            *frame,
            cv::Size{frame->width, frame->height},
            SWS_BILINEAR),
        rotation_degrees);
  };
}

cv::Size boundedDisplaySize(
    cv::Size source_size,
    int maximum_width,
    int maximum_height) {
  if (maximum_width <= 0 || maximum_height <= 0) {
    throw std::invalid_argument("preview dimensions must be positive");
  }
  const double scale = std::min({
      1.0,
      static_cast<double>(maximum_width) / source_size.width,
      static_cast<double>(maximum_height) / source_size.height});
  return {
      std::max(1, static_cast<int>(std::lround(source_size.width * scale))),
      std::max(1, static_cast<int>(std::lround(source_size.height * scale)))};
}

std::function<cv::Mat(int, int)> previewBgrRenderer(
    std::shared_ptr<AVFrame> frame,
    int rotation_degrees) {
  return [frame = std::move(frame), rotation_degrees](
             int maximum_width,
             int maximum_height) {
    const bool swaps_dimensions = rotation_degrees == 90 || rotation_degrees == 270;
    const cv::Size display_size = swaps_dimensions
        ? cv::Size{frame->height, frame->width}
        : cv::Size{frame->width, frame->height};
    cv::Size destination_size = boundedDisplaySize(
        display_size, maximum_width, maximum_height);
    if (swaps_dimensions) {
      std::swap(destination_size.width, destination_size.height);
    }

    return applyDisplayRotation(
        convertToBgrOnce(*frame, destination_size, SWS_AREA),
        rotation_degrees);
  };
}

std::string avError(int code) {
  std::array<char, AV_ERROR_MAX_STRING_SIZE> buffer{};
  av_strerror(code, buffer.data(), buffer.size());
  return std::string{buffer.data()};
}

void checkAv(int code, const std::string& operation) {
  if (code < 0) {
    throw std::runtime_error(operation + ": " + avError(code));
  }
}

AVPixelFormat selectHardwarePixelFormat(
    AVCodecContext* context,
    const AVPixelFormat* formats) {
  const auto requested = *static_cast<const AVPixelFormat*>(context->opaque);
  for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
    if (*format == requested) {
      return *format;
    }
  }
  for (const AVPixelFormat* format = formats; *format != AV_PIX_FMT_NONE; ++format) {
    const auto* descriptor = av_pix_fmt_desc_get(*format);
    if (descriptor != nullptr
        && (descriptor->flags & AV_PIX_FMT_FLAG_HWACCEL) == 0) {
      return *format;
    }
  }
  return AV_PIX_FMT_NONE;
}

Rational rational(AVRational value) {
  return Rational{value.num, value.den == 0 ? 1 : value.den};
}

int displayRotation(const AVCodecParameters& parameters) {
  const auto* side_data = av_packet_side_data_get(
      parameters.coded_side_data,
      parameters.nb_coded_side_data,
      AV_PKT_DATA_DISPLAYMATRIX);
  if (side_data == nullptr || side_data->size < 9U * sizeof(std::int32_t)) {
    return 0;
  }
  const auto* matrix = reinterpret_cast<const std::int32_t*>(side_data->data);
  const double angle = av_display_rotation_get(matrix);
  if (!std::isfinite(angle)) {
    return 0;
  }
  int normalized = static_cast<int>(std::lround(angle)) % 360;
  if (normalized < 0) {
    normalized += 360;
  }
  if (normalized % 90 != 0) {
    throw std::runtime_error(
        "Only right-angle video display rotation is supported; found "
        + std::to_string(angle) + " degrees");
  }
  return normalized;
}

}  // namespace

class VideoDecoder::Impl {
 public:
  explicit Impl(
      const std::filesystem::path& path,
      const VideoDecoderOptions& options)
      : defer_full_bgr_{
            options.defer_full_bgr
            || options.target_analysis_area_px.has_value()} {
    const auto encoded_path = utf8Path(path);
    AVFormatContext* opened_format = nullptr;
    checkAv(
        avformat_open_input(&opened_format, encoded_path.c_str(), nullptr, nullptr),
        "Cannot open video " + encoded_path);
    format_ = opened_format;
    try {
      checkAv(avformat_find_stream_info(format_, nullptr), "Cannot read stream information");
      const int stream_index = av_find_best_stream(
          format_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
      checkAv(stream_index, "Cannot find a video stream");
      stream_index_ = stream_index;
      stream_ = format_->streams[stream_index_];

      const AVCodec* codec = avcodec_find_decoder(stream_->codecpar->codec_id);
      if (codec == nullptr) {
        throw std::runtime_error("No decoder is available for the video stream");
      }
      codec_ = avcodec_alloc_context3(codec);
      if (codec_ == nullptr) {
        throw std::bad_alloc{};
      }
      checkAv(
          avcodec_parameters_to_context(codec_, stream_->codecpar),
          "Cannot copy video codec parameters");
      constexpr unsigned int maximum_decoder_threads = 16U;
      codec_->thread_count = static_cast<int>(std::clamp(
          std::thread::hardware_concurrency(), 1U, maximum_decoder_threads));
      codec_->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
      configureHardwareDecode(codec);
      checkAv(avcodec_open2(codec_, codec, nullptr), "Cannot open video decoder");

      packet_ = av_packet_alloc();
      frame_ = av_frame_alloc();
      software_frame_ = av_frame_alloc();
      if (packet_ == nullptr || frame_ == nullptr || software_frame_ == nullptr) {
        throw std::bad_alloc{};
      }

      info_.rotation_degrees = displayRotation(*stream_->codecpar);
      const bool swaps_dimensions = info_.rotation_degrees == 90
          || info_.rotation_degrees == 270;
      info_.width = swaps_dimensions ? codec_->height : codec_->width;
      info_.height = swaps_dimensions ? codec_->width : codec_->height;
      info_.time_base = rational(stream_->time_base);
      info_.average_frame_rate = rational(stream_->avg_frame_rate);
      if (stream_->nb_frames > 0) {
        info_.reported_frame_count = stream_->nb_frames;
      }
      if (stream_->duration != AV_NOPTS_VALUE) {
        info_.duration_seconds = rational(stream_->time_base).toSeconds(stream_->duration);
      } else if (format_->duration != AV_NOPTS_VALUE) {
        info_.duration_seconds = static_cast<double>(format_->duration)
            / static_cast<double>(AV_TIME_BASE);
      }
      if (stream_->start_time != AV_NOPTS_VALUE) {
        info_.start_time_seconds = rational(stream_->time_base).toSeconds(stream_->start_time);
      }
      info_.exact_frame_indices_after_seek = detail::hasStableSeekFrameIndices(
          info_.average_frame_rate,
          rational(stream_->r_frame_rate),
          info_.reported_frame_count,
          info_.duration_seconds);
      info_.codec_name = codec->name != nullptr ? codec->name : "unknown";
      if (options.target_analysis_area_px) {
        analysis_frame_size_ = analysisFrameSize(
            cv::Size{info_.width, info_.height},
            *options.target_analysis_area_px);
      }
    } catch (...) {
      cleanup();
      throw;
    }
  }

  ~Impl() { cleanup(); }

  const VideoInfo& info() const { return info_; }

  bool seekToSeconds(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
      throw std::invalid_argument("seek time must be finite and non-negative");
    }
    const auto microseconds = static_cast<std::int64_t>(
        std::llround(seconds * static_cast<double>(AV_TIME_BASE)));
    std::int64_t target = av_rescale_q(
        microseconds, AVRational{1, AV_TIME_BASE}, stream_->time_base);
    if (stream_->start_time != AV_NOPTS_VALUE) {
      target += stream_->start_time;
    }
    if (av_seek_frame(format_, stream_index_, target, AVSEEK_FLAG_BACKWARD) < 0) {
      return false;
    }
    avcodec_flush_buffers(codec_);
    av_packet_unref(packet_);
    av_frame_unref(frame_);
    input_eof_ = false;
    flush_sent_ = false;
    random_access_mode_ = true;
    return true;
  }

  std::optional<DecodedFrame> read() {
    const auto read_started = Clock::now();
    while (true) {
      const int receive_result = avcodec_receive_frame(codec_, frame_);
      if (receive_result == 0) {
        const double packet_decode_seconds = elapsedSeconds(read_started);
        const AVFrame* decoded_frame = frame_;
        double hardware_transfer_seconds = 0.0;
        if (frame_->format == hardware_pixel_format_) {
          info_.hardware_accelerated_decode = true;
          av_frame_unref(software_frame_);
          const auto transfer_started = Clock::now();
          checkAv(
              av_hwframe_transfer_data(software_frame_, frame_, 0),
              "Cannot transfer hardware-decoded video frame");
          checkAv(
              av_frame_copy_props(software_frame_, frame_),
              "Cannot copy hardware-decoded frame metadata");
          hardware_transfer_seconds = elapsedSeconds(transfer_started);
          decoded_frame = software_frame_;
        }
        return convertFrame(
            *decoded_frame, packet_decode_seconds, hardware_transfer_seconds);
      }
      if (receive_result == AVERROR_EOF) {
        return std::nullopt;
      }
      if (receive_result != AVERROR(EAGAIN)) {
        checkAv(receive_result, "Video decode failed");
      }

      if (input_eof_) {
        if (!flush_sent_) {
          const int flush_result = avcodec_send_packet(codec_, nullptr);
          if (flush_result != AVERROR_EOF) {
            checkAv(flush_result, "Cannot flush video decoder");
          }
          flush_sent_ = true;
          continue;
        }
        throw std::runtime_error("Video decoder requested input after its flush packet");
      }

      bool submitted_video_packet = false;
      while (!submitted_video_packet) {
        const int read_result = av_read_frame(format_, packet_);
        if (read_result == AVERROR_EOF) {
          input_eof_ = true;
          break;
        }
        checkAv(read_result, "Cannot read video packet");
        if (packet_->stream_index == stream_index_) {
          const int send_result = avcodec_send_packet(codec_, packet_);
          av_packet_unref(packet_);
          checkAv(send_result, "Cannot submit video packet to decoder");
          submitted_video_packet = true;
        } else {
          av_packet_unref(packet_);
        }
      }
    }
  }

 private:
  void configureHardwareDecode(const AVCodec* codec) {
#if defined(__APPLE__)
    constexpr std::int64_t minimum_hardware_decode_area = 1920LL * 1080LL;
    if (static_cast<std::int64_t>(codec_->width) * codec_->height
        < minimum_hardware_decode_area) {
      return;
    }
    for (int index = 0;; ++index) {
      const AVCodecHWConfig* config = avcodec_get_hw_config(codec, index);
      if (config == nullptr) {
        return;
      }
      if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) == 0
          || config->device_type != AV_HWDEVICE_TYPE_VIDEOTOOLBOX) {
        continue;
      }
      AVBufferRef* device = nullptr;
      if (av_hwdevice_ctx_create(
              &device, config->device_type, nullptr, nullptr, 0) < 0) {
        return;
      }
      AVBufferRef* device_reference = av_buffer_ref(device);
      av_buffer_unref(&device);
      if (device_reference == nullptr) {
        return;
      }
      hardware_pixel_format_ = config->pix_fmt;
      codec_->opaque = &hardware_pixel_format_;
      codec_->get_format = selectHardwarePixelFormat;
      codec_->hw_device_ctx = device_reference;
      return;
    }
#else
    (void)codec;
#endif
  }

  DecodedFrame convertFrame(
      const AVFrame& frame,
      double packet_decode_seconds,
      double hardware_transfer_seconds) {
    cv::Size destination_size{frame.width, frame.height};
    if (analysis_frame_size_) {
      destination_size = *analysis_frame_size_;
      if (info_.rotation_degrees == 90 || info_.rotation_degrees == 270) {
        std::swap(destination_size.width, destination_size.height);
      }
    }

    const auto conversion_started = Clock::now();
    cv::Mat converted;
    AnalysisConversionMethod analysis_conversion_method =
        AnalysisConversionMethod::none;
    if (!analysis_frame_size_ && !defer_full_bgr_) {
      converted = convertToBgr(frame, destination_size, SWS_BILINEAR, sws_);
    } else if (analysis_frame_size_) {
      const bool downsampling =
          static_cast<std::int64_t>(frame.width) * frame.height
          > static_cast<std::int64_t>(destination_size.width)
              * destination_size.height;
      auto native = downsampling
          ? convertNativeLumaWithOpenCv(frame, destination_size)
          : std::nullopt;
      if (native) {
        converted = std::move(*native);
        analysis_conversion_method =
            AnalysisConversionMethod::opencv_native_luma;
      } else {
        converted = convertToGray(frame, destination_size, sws_);
        analysis_conversion_method =
            AnalysisConversionMethod::ffmpeg_fallback;
      }
    }
    const double pixel_conversion_seconds = elapsedSeconds(conversion_started);

    const auto rotation_started = Clock::now();
    if (!converted.empty()) {
      converted = applyDisplayRotation(
          std::move(converted), info_.rotation_degrees);
    }
    const double rotation_seconds = elapsedSeconds(rotation_started);

    cv::Mat bgr;
    cv::Mat analysis_gray;
    std::function<cv::Mat()> render_bgr;
    std::function<cv::Mat(int, int)> render_preview_bgr;
    if (analysis_frame_size_) {
      analysis_gray = std::move(converted);
    } else if (!defer_full_bgr_) {
      bgr = std::move(converted);
    }
    if (defer_full_bgr_) {
      auto retained_frame = cloneFrame(frame);
      render_bgr = fullBgrRenderer(retained_frame, info_.rotation_degrees);
      render_preview_bgr = previewBgrRenderer(
          std::move(retained_frame), info_.rotation_degrees);
    }

    const std::int64_t raw_pts = frame.best_effort_timestamp;
    const auto pts = raw_pts == AV_NOPTS_VALUE
        ? std::optional<std::int64_t>{}
        : std::optional<std::int64_t>{raw_pts};
    std::int64_t decoded_index = next_decoded_index_++;
    const auto fps = info_.framesPerSecond();
    if (random_access_mode_ && pts && fps) {
      const std::int64_t origin = stream_->start_time == AV_NOPTS_VALUE
          ? 0
          : stream_->start_time;
      const double seconds = info_.time_base.toSeconds(*pts - origin);
      decoded_index = std::max<std::int64_t>(
          0,
          static_cast<std::int64_t>(
              std::llround(seconds * *fps)));
      next_decoded_index_ = decoded_index + 1;
    }
    return DecodedFrame{
        std::move(bgr),
        decoded_index,
        pts,
        info_.time_base,
        FrameDecodeTimings{
            packet_decode_seconds,
            hardware_transfer_seconds,
            pixel_conversion_seconds,
            rotation_seconds,
            analysis_conversion_method},
        std::move(analysis_gray),
        std::move(render_bgr),
        std::move(render_preview_bgr)};
  }

  void cleanup() noexcept {
    sws_freeContext(sws_);
    sws_ = nullptr;
    av_frame_free(&frame_);
    av_frame_free(&software_frame_);
    av_packet_free(&packet_);
    avcodec_free_context(&codec_);
    avformat_close_input(&format_);
  }

  AVFormatContext* format_{nullptr};
  AVCodecContext* codec_{nullptr};
  AVPacket* packet_{nullptr};
  AVFrame* frame_{nullptr};
  AVFrame* software_frame_{nullptr};
  SwsContext* sws_{nullptr};
  AVStream* stream_{nullptr};
  int stream_index_{-1};
  VideoInfo info_{};
  std::optional<cv::Size> analysis_frame_size_{};
  bool defer_full_bgr_{};
  AVPixelFormat hardware_pixel_format_{AV_PIX_FMT_NONE};
  std::int64_t next_decoded_index_{0};
  bool input_eof_{false};
  bool flush_sent_{false};
  bool random_access_mode_{false};
};

double Rational::toSeconds(std::int64_t value) const {
  if (denominator == 0) {
    throw std::invalid_argument("rational denominator must not be zero");
  }
  return static_cast<double>(value) * static_cast<double>(numerator)
      / static_cast<double>(denominator);
}

std::optional<double> VideoInfo::framesPerSecond() const {
  if (average_frame_rate.numerator <= 0 || average_frame_rate.denominator <= 0) {
    return std::nullopt;
  }
  return static_cast<double>(average_frame_rate.numerator)
      / static_cast<double>(average_frame_rate.denominator);
}

std::optional<double> VideoInfo::estimatedDurationSeconds() const {
  if (duration_seconds && *duration_seconds > 0.0) {
    return duration_seconds;
  }
  const auto fps = framesPerSecond();
  if (!reported_frame_count || *reported_frame_count < 0 || !fps) {
    return std::nullopt;
  }
  return static_cast<double>(*reported_frame_count) / *fps;
}

std::optional<double> DecodedFrame::ptsSeconds() const {
  return pts ? std::optional<double>{time_base.toSeconds(*pts)} : std::nullopt;
}

const cv::Mat& DecodedFrame::fullBgr() const {
  if (bgr.empty() && render_bgr) {
    bgr = render_bgr();
  }
  if (bgr.empty()) {
    throw std::runtime_error("decoded frame has no full-resolution BGR representation");
  }
  return bgr;
}

cv::Mat DecodedFrame::previewBgr(int maximum_width, int maximum_height) const {
  if (bgr.empty() && render_preview_bgr) {
    return render_preview_bgr(maximum_width, maximum_height);
  }

  const auto& full = fullBgr();
  const cv::Size target = boundedDisplaySize(
      full.size(), maximum_width, maximum_height);
  if (target == full.size()) {
    return full;
  }
  cv::Mat preview;
  cv::resize(full, preview, target, 0.0, 0.0, cv::INTER_AREA);
  return preview;
}

double relativeTimestampSeconds(const DecodedFrame& frame, const VideoInfo& info) {
  if (const auto seconds = frame.ptsSeconds()) {
    return *seconds - info.start_time_seconds.value_or(0.0);
  }
  if (const auto fps = info.framesPerSecond()) {
    return static_cast<double>(frame.decoded_frame_index) / *fps;
  }
  return static_cast<double>(frame.decoded_frame_index) / 30.0;
}

VideoDecoder::VideoDecoder(
    const std::filesystem::path& path,
    VideoDecoderOptions options)
    : impl_{std::make_unique<Impl>(path, options)} {}

VideoDecoder::~VideoDecoder() = default;
VideoDecoder::VideoDecoder(VideoDecoder&&) noexcept = default;
VideoDecoder& VideoDecoder::operator=(VideoDecoder&&) noexcept = default;

const VideoInfo& VideoDecoder::info() const { return impl_->info(); }
std::optional<DecodedFrame> VideoDecoder::read() { return impl_->read(); }
bool VideoDecoder::seekToSeconds(double seconds) {
  return impl_->seekToSeconds(seconds);
}

}  // namespace frame_extractor
