/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/audio_processing/agc2/fixed_digital_level_estimator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

#include "api/array_view.h"
#include "api/audio/audio_frame.h"
#include "api/audio/audio_view.h"
#include "modules/audio_processing/agc2/agc2_common.h"
#include "modules/audio_processing/logging/apm_data_dumper.h"
#include "rtc_base/checks.h"

namespace webrtc {
namespace {

constexpr float kInitialFilterStateLevel = 0.0f;

// Instant attack.
constexpr float kAttackFilterConstant = 0.0f;

// Limiter decay constant.
// Computed as `10 ** (-1/20 * subframe_duration / kDecayMs)` where:
// - `subframe_duration` is `kFrameDurationMs / kSubFramesInFrame`;
// - `kDecayMs` is defined in agc2_testing_common.h.
constexpr float kDecayFilterConstant = 0.9971259f;

}  // namespace

FixedDigitalLevelEstimator::FixedDigitalLevelEstimator(
    size_t samples_per_channel,
    ApmDataDumper* apm_data_dumper)
    : apm_data_dumper_(apm_data_dumper),
      filter_state_level_(kInitialFilterStateLevel) {
  SetSamplesPerChannel(samples_per_channel);
  CheckParameterCombination();
  RTC_DCHECK(apm_data_dumper_);
  // Convert `samples_per_channel` to sample rate for
  // `agc2_level_estimator_samplerate`.
  apm_data_dumper_->DumpRaw("agc2_level_estimator_samplerate",
                            samples_per_channel * kDefaultAudioBuffersPerSec);
}

void FixedDigitalLevelEstimator::CheckParameterCombination() {
  RTC_DCHECK_GT(samples_in_frame_, 0);
  RTC_DCHECK_LE(kSubFramesInFrame, samples_in_frame_);
  RTC_DCHECK_EQ(samples_in_frame_ % kSubFramesInFrame, 0);
  RTC_DCHECK_GT(samples_in_sub_frame_, 1);
}

std::array<float, kSubFramesInFrame> FixedDigitalLevelEstimator::ComputeLevel(
    DeinterleavedView<const float> float_frame) {
  RTC_DCHECK_GT(float_frame.num_channels(), 0);
  RTC_DCHECK_EQ(float_frame.samples_per_channel(), samples_in_frame_);

  // Compute max envelope without smoothing.
  std::array<float, kSubFramesInFrame> envelope{};
  for (size_t channel_idx = 0; channel_idx < float_frame.num_channels();
       ++channel_idx) {
    const auto channel = float_frame[channel_idx];
    for (int sub_frame = 0; sub_frame < kSubFramesInFrame; ++sub_frame) {
      for (int sample_in_sub_frame = 0;
           sample_in_sub_frame < samples_in_sub_frame_; ++sample_in_sub_frame) {
        envelope[sub_frame] =
            std::max(envelope[sub_frame],
                     std::abs(channel[sub_frame * samples_in_sub_frame_ +
                                      sample_in_sub_frame]));
      }
    }
  }

  // Make sure envelope increases happen one step earlier so that the
  // corresponding *gain decrease* doesn't miss a sudden signal
  // increase due to interpolation.
  for (int sub_frame = 0; sub_frame < kSubFramesInFrame - 1; ++sub_frame) {
    if (envelope[sub_frame] < envelope[sub_frame + 1]) {
      envelope[sub_frame] = envelope[sub_frame + 1];
    }
  }

  // Add attack / decay smoothing.
  for (int sub_frame = 0; sub_frame < kSubFramesInFrame; ++sub_frame) {
    const float envelope_value = envelope[sub_frame];
    if (envelope_value > filter_state_level_) {
      envelope[sub_frame] = envelope_value * (1 - kAttackFilterConstant) +
                            filter_state_level_ * kAttackFilterConstant;
    } else {
      envelope[sub_frame] = envelope_value * (1 - kDecayFilterConstant) +
                            filter_state_level_ * kDecayFilterConstant;
    }
    filter_state_level_ = envelope[sub_frame];

    // Dump data for debug.
    RTC_DCHECK(apm_data_dumper_);
    const auto channel = float_frame[0];
    apm_data_dumper_->DumpRaw("agc2_level_estimator_samples",
                              samples_in_sub_frame_,
                              &channel[sub_frame * samples_in_sub_frame_]);
    apm_data_dumper_->DumpRaw("agc2_level_estimator_level",
                              envelope[sub_frame]);
  }

  return envelope;
}

void FixedDigitalLevelEstimator::SetSamplesPerChannel(
    size_t samples_per_channel) {
  samples_in_frame_ = static_cast<int>(samples_per_channel);
  samples_in_sub_frame_ = CheckedDivExact(samples_in_frame_, kSubFramesInFrame);
  CheckParameterCombination();
}

void FixedDigitalLevelEstimator::Reset() {
  filter_state_level_ = kInitialFilterStateLevel;
}

}  // namespace webrtc
