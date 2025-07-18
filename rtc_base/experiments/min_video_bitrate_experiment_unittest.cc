/*
 *  Copyright 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "rtc_base/experiments/min_video_bitrate_experiment.h"

#include <optional>

#include "api/field_trials.h"
#include "api/units/data_rate.h"
#include "api/video/video_codec_type.h"
#include "test/gtest.h"

namespace webrtc {
namespace {

TEST(GetExperimentalMinVideoBitrateTest,
     NulloptForAllCodecsIfFieldTrialUndefined) {
  FieldTrials field_trials("");

  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecGeneric),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP8),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP9),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecH264),
            std::nullopt);
}

TEST(GetExperimentalMinVideoBitrateTest,
     NulloptForAllCodecsIfFieldTrialDisabled) {
  FieldTrials field_trials("WebRTC-Video-MinVideoBitrate/Disabled,br:123kbps/");

  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecGeneric),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP8),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP9),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecH264),
            std::nullopt);
}

TEST(GetExperimentalMinVideoBitrateTest, BrForAllCodecsIfDefined) {
  FieldTrials field_trials("WebRTC-Video-MinVideoBitrate/Enabled,br:123kbps/");

  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecGeneric),
            DataRate::KilobitsPerSec(123));
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP8),
            DataRate::KilobitsPerSec(123));
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP9),
            DataRate::KilobitsPerSec(123));
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecH264),
            DataRate::KilobitsPerSec(123));
}

TEST(GetExperimentalMinVideoBitrateTest, BrTrumpsSpecificCodecConfigs) {
  FieldTrials field_trials(
      "WebRTC-Video-MinVideoBitrate/"
      "Enabled,br:123kbps,vp8_br:100kbps,vp9_br:200kbps,h264_br:300kbps/");

  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecGeneric),
            DataRate::KilobitsPerSec(123));
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP8),
            DataRate::KilobitsPerSec(123));
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP9),
            DataRate::KilobitsPerSec(123));
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecH264),
            DataRate::KilobitsPerSec(123));
}

TEST(GetExperimentalMinVideoBitrateTest,
     SpecificCodecConfigsIgnoredIfExpDisabled) {
  FieldTrials field_trials(
      "WebRTC-Video-MinVideoBitrate/"
      "Disabled,vp8_br:100kbps,vp9_br:200kbps,h264_br:300kbps/");

  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecGeneric),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP8),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP9),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecH264),
            std::nullopt);
}

TEST(GetExperimentalMinVideoBitrateTest, SpecificCodecConfigsUsedIfExpEnabled) {
  FieldTrials field_trials(
      "WebRTC-Video-MinVideoBitrate/"
      "Enabled,vp8_br:100kbps,vp9_br:200kbps,h264_br:300kbps/");

  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecGeneric),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP8),
            DataRate::KilobitsPerSec(100));
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP9),
            DataRate::KilobitsPerSec(200));
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecH264),
            DataRate::KilobitsPerSec(300));
}

TEST(GetExperimentalMinVideoBitrateTest,
     Vp8BitrateValueTakenFromFallbackIfAvailable) {
  FieldTrials field_trials(
      "WebRTC-Video-MinVideoBitrate/"
      "Enabled,vp8_br:100kbps,vp9_br:200kbps,h264_br:300kbps/"
      "WebRTC-VP8-Forced-Fallback-Encoder-v2/"
      "Enabled-444444,555555,666666/");

  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP8),
            DataRate::BitsPerSec(666666));
}

TEST(GetExperimentalMinVideoBitrateTest,
     NonVp8BitrateValuesTakenFromMinVideoBitrate) {
  FieldTrials field_trials(
      "WebRTC-Video-MinVideoBitrate/"
      "Enabled,vp8_br:100kbps,vp9_br:200kbps,h264_br:300kbps/"
      "WebRTC-VP8-Forced-Fallback-Encoder-v2/"
      "Enabled-444444,555555,666666/");

  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecGeneric),
            std::nullopt);
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecVP9),
            DataRate::KilobitsPerSec(200));
  EXPECT_EQ(GetExperimentalMinVideoBitrate(field_trials, kVideoCodecH264),
            DataRate::KilobitsPerSec(300));
}

}  // namespace
}  // namespace webrtc
