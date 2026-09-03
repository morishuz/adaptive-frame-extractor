#include "frame_extractor/diagnostics.hpp"
#include "frame_extractor/tracking.hpp"

#include "flow_sampling.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <opencv2/core.hpp>

#include <limits>
#include <vector>

namespace fe = frame_extractor;
using Catch::Approx;

TEST_CASE("analysis grid uses stable pixel spacing") {
  fe::Config config;
  const auto state = fe::initializeTrackingState(100, 60, config);
  const std::vector<fe::Point2f> expected{
      {4.0F, 4.0F}, {44.0F, 4.0F}, {84.0F, 4.0F},
      {4.0F, 44.0F}, {44.0F, 44.0F}, {84.0F, 44.0F}};
  CHECK(state.origin_points == expected);
  CHECK(state.alive_mask == std::vector<std::uint8_t>(expected.size(), 1U));
}

TEST_CASE("bilinear flow sampling handles interpolation and boundaries") {
  cv::Mat flow(2, 2, CV_32FC2);
  flow.at<cv::Vec2f>(0, 0) = {0.0F, 0.0F};
  flow.at<cv::Vec2f>(0, 1) = {2.0F, 4.0F};
  flow.at<cv::Vec2f>(1, 0) = {4.0F, 8.0F};
  flow.at<cv::Vec2f>(1, 1) = {6.0F, 12.0F};
  const auto sample = [&flow](std::span<const fe::Point2f> points) {
    return fe::detail::sampleBilinearFlow(
        flow.cols,
        flow.rows,
        points,
        [&flow](int x, int y) {
          const auto value = flow.at<cv::Vec2f>(y, x);
          return fe::Point2f{value[0], value[1]};
        });
  };
  const std::vector<fe::Point2f> interior{{0.5F, 0.25F}};
  const auto sampled = sample(interior);
  CHECK(sampled.values[0].x == Approx(2.0));
  CHECK(sampled.values[0].y == Approx(4.0));
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const std::vector<fe::Point2f> edges{{1.0F, 1.0F}, {-0.01F, 0.0F}, {2.0F, 1.0F}, {nan, 0.0F}};
  const auto edge_samples = sample(edges);
  CHECK(edge_samples.valid_mask == std::vector<std::uint8_t>{1U, 0U, 0U, 0U});
  CHECK(edge_samples.values[0] == fe::Point2f{6.0F, 12.0F});
}

TEST_CASE("image and lost-border masks have distinct boundaries") {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const std::vector<fe::Point2f> points{
      {0.0F, 0.0F}, {9.0F, 7.0F}, {-1.0F, 2.0F}, {-2.0F, 2.0F},
      {-2.01F, 2.0F}, {11.0F, 2.0F}, {11.01F, 2.0F}, {nan, 1.0F}};
  CHECK(fe::insideImage(points, 10, 8)
        == std::vector<std::uint8_t>{1U, 1U, 0U, 0U, 0U, 0U, 0U, 0U});
  CHECK(fe::beyondLostBorder(points, 10, 8, 2.0)
        == std::vector<std::uint8_t>{0U, 0U, 0U, 0U, 1U, 0U, 1U, 1U});
}

TEST_CASE("flow application clips motion and permanently loses points") {
  fe::Config config;
  config.max_step_norm_analysis_px = 5.0;
  config.sampling.lost_border_analysis_px = 1.0;
  fe::TrackingState state{{{1.0F, 1.0F}}, {{1.0F, 1.0F}}, {1U}};
  const fe::detail::SampledFlow flow{{{6.0F, 8.0F}}, {1U}};
  const auto diagnostics = fe::detail::applySampledFlow(state, flow, 3, 3, config);
  CHECK(state.current_points[0].x == Approx(4.0));
  CHECK(state.current_points[0].y == Approx(5.0));
  CHECK(diagnostics.in_bounds_mask == std::vector<std::uint8_t>{0U});
  CHECK(state.alive_mask == std::vector<std::uint8_t>{0U});
}

TEST_CASE("percentile and frame scores match NumPy semantics") {
  const std::vector<double> values{0.0, 10.0, 20.0, 30.0, std::numeric_limits<double>::quiet_NaN()};
  CHECK(fe::linearPercentile(values, 50.0) == Approx(15.0));
  CHECK(fe::linearPercentile(values, 80.0) == Approx(24.0));
  CHECK(fe::linearPercentile({}, 80.0) == Approx(0.0));

  fe::Config config;
  config.scoring.percentile = 50.0;
  const fe::TrackingState state{
      {{0.0F, 0.0F}, {0.0F, 0.0F}, {0.0F, 0.0F}},
      {{1.5F, 2.0F}, {3.0F, 4.0F}, {10.0F, 10.0F}},
      {1U, 1U, 0U}};
  const auto scores = fe::computeFrameScores(state, {{1U, 1U, 1U}}, 7, 0.25, config);
  CHECK(scores.global_score == Approx(3.75));
  CHECK(scores.in_bounds_points == 2U);
  CHECK(scores.in_bounds_ratio == Approx(2.0 / 3.0));
}

TEST_CASE("triggers preserve cause order and interval age behavior") {
  fe::TriggerConfig config;
  config.min_frames_since_keyframe = 2;
  config.main_threshold_analysis_px = 10.0;
  config.min_in_bounds_ratio = 0.5;
  config.max_frames_since_keyframe = 3;
  const fe::FrameScores scores{4, 0.4, 10.0, 1U, 0.25};
  CHECK_FALSE(fe::decideTrigger(scores, 1, config).triggered);
  const auto combined = fe::decideTrigger(scores, 3, config);
  CHECK(combined.reason == "main+in_bounds+interval");
  CHECK(combined.displayReason() == "motion+low_points+interval");
  config.min_frames_since_keyframe = 10;
  config.max_frames_since_keyframe = 2;
  CHECK(fe::decideTrigger({2, 0.2, 0.0, 10U, 1.0}, 2, config).reason == "interval");
}

TEST_CASE("cancellation token supports event consumers") {
  fe::CancellationToken token;
  CHECK_FALSE(token.isCancellationRequested());
  token.requestCancellation();
  CHECK(token.isCancellationRequested());
}
