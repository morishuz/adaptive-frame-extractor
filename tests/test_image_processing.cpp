#include "frame_extractor/image_processing.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <opencv2/imgproc.hpp>

#include <cstdint>

namespace fe = frame_extractor;
using Catch::Approx;

TEST_CASE("analysis sizing preserves aspect ratio across source shapes") {
  CHECK(fe::analysisFrameSize({3840, 2160}, 518400) == cv::Size{960, 540});
  CHECK(fe::analysisFrameSize({2160, 3840}, 518400) == cv::Size{540, 960});
  CHECK(fe::analysisFrameSize({7680, 1080}, 518400) == cv::Size{1920, 270});
  CHECK(fe::analysisFrameSize({1080, 7680}, 518400) == cv::Size{270, 1920});
  CHECK(fe::analysisFrameSize({960, 480}, 518400) == cv::Size{960, 480});

  const auto odd = fe::analysisFrameSize({4033, 1011}, 518400);
  CHECK(static_cast<double>(odd.width) / static_cast<double>(odd.height)
        == Approx(4033.0 / 1011.0).epsilon(0.005));
  CHECK(static_cast<double>(odd.area()) == Approx(518400.0).epsilon(0.005));
  CHECK_THROWS_AS(fe::analysisFrameSize({0, 10}, 518400), std::invalid_argument);
  CHECK_THROWS_AS(fe::analysisFrameSize({10, 10}, 0), std::invalid_argument);
}

TEST_CASE("analysis resize downsamples without enlarging small inputs") {
  cv::Mat bgr(180, 320, CV_8UC3);
  for (int y = 0; y < bgr.rows; ++y) {
    for (int x = 0; x < bgr.cols; ++x) {
      bgr.at<cv::Vec3b>(y, x) = cv::Vec3b{
          static_cast<std::uint8_t>((x * 7) % 256),
          static_cast<std::uint8_t>((y * 11) % 256),
          static_cast<std::uint8_t>(((x + y) * 13) % 256)};
    }
  }
  const auto resized = fe::resizeForAnalysis(bgr, 14400);
  CHECK(resized.size() == cv::Size{160, 90});
  const auto unchanged = fe::resizeForAnalysis(bgr, 518400);
  CHECK(unchanged.size() == bgr.size());
  CHECK(unchanged.data == bgr.data);
  const auto gray = fe::ensureGray(resized);
  CHECK(gray.type() == CV_8UC1);
  CHECK(gray.size() == resized.size());
  CHECK(fe::ensureGray(gray).data == gray.data);
}

TEST_CASE("DIS configuration applies every reference option") {
  fe::DisConfig config;
  config.preset = fe::DisPreset::fast;
  config.finest_scale = 2;
  config.patch_size = 12;
  config.patch_stride = 5;
  config.gradient_descent_iterations = 7;
  config.variational_refinement_iterations = 1;
  config.use_spatial_propagation = false;
  const auto solver = fe::createDisFlow(config);
  CHECK(solver->getFinestScale() == 2);
  CHECK(solver->getPatchSize() == 12);
  CHECK(solver->getPatchStride() == 5);
  CHECK(solver->getGradientDescentIterations() == 7);
  CHECK(solver->getVariationalRefinementIterations() == 1);
  CHECK_FALSE(solver->getUseSpatialPropagation());
}

TEST_CASE("DIS matches the Python OpenCV 4.13 translation oracle") {
  cv::Mat previous = cv::Mat::zeros(96, 128, CV_8UC1);
  cv::rectangle(previous, cv::Rect{24, 20, 60, 48}, cv::Scalar{220}, cv::FILLED);
  cv::line(previous, cv::Point{10, 80}, cv::Point{110, 10}, cv::Scalar{90}, 3);
  const cv::Mat transform = (cv::Mat_<double>(2, 3) << 1.0, 0.0, 3.0, 0.0, 1.0, 2.0);
  cv::Mat current;
  cv::warpAffine(previous, current, transform, previous.size(), cv::INTER_LINEAR, cv::BORDER_REFLECT101);
  fe::Config config;
  config.sampling.grid_step_analysis_px = 16;
  config.sampling.min_margin_analysis_px = 16;
  auto solver = fe::createDisFlow(config.dis);
  auto state = fe::initializeTrackingState(previous.cols, previous.rows, config);
  fe::TrackingStepTimings timings;
  const auto diagnostics = fe::stepTracking(
      state, previous, current, *solver, config, &timings);
  const auto scores = fe::computeFrameScores(state, diagnostics, 1, 0.1, config);
  CHECK(state.origin_points.size() == 24U);
  CHECK(scores.global_score == Approx(3.6585294723510744).margin(1.0e-5));
  CHECK(scores.in_bounds_points == 24U);
  CHECK(timings.dense_flow_seconds >= 0.0);
  CHECK(timings.point_sampling_seconds >= 0.0);
  CHECK(scores.in_bounds_ratio == 1.0);
  CHECK(state.current_points.front().x == Approx(18.85783576965332).margin(1.0e-5));
  CHECK(state.current_points.front().y == Approx(18.10630226135254).margin(1.0e-5));
  CHECK(state.current_points.back().x == Approx(96.70097351074219).margin(1.0e-5));
  CHECK(state.current_points.back().y == Approx(64.55143737792969).margin(1.0e-5));
}
