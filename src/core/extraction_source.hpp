#pragma once

#include "frame_extractor/extraction_run.hpp"

#include <memory>

namespace frame_extractor::detail {

using ExtractionSourceFactory = std::function<std::unique_ptr<FrameSource>(
    const ExtractionRunRequest&)>;

// Internal source injection keeps run lifecycle tests independent of hardware.
[[nodiscard]] ExtractionRunResult runExtractionWithSource(
    ExtractionRunRequest request,
    const ExtractionSourceFactory& source_factory,
    DiagnosticObserver* observer = nullptr,
    const CancellationToken* cancellation = nullptr,
    SelectedFrameSink* additional_selected_frame_sink = nullptr,
    FramePreviewSink* frame_preview_sink = nullptr,
    RunOutputStartedCallback output_started = {});

}  // namespace frame_extractor::detail
