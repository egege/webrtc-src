/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/rtp_rtcp/source/ntp_time_util.h"

#include <algorithm>
#include <cstdint>

#include "api/units/time_delta.h"
#include "rtc_base/numerics/divide_round.h"
#include "rtc_base/time_utils.h"

namespace webrtc {

uint32_t SaturatedToCompactNtp(TimeDelta delta) {
  constexpr uint32_t kMaxCompactNtp = 0xFFFFFFFF;
  constexpr int kCompactNtpInSecond = 0x10000;
  if (delta <= TimeDelta::Zero())
    return 0;
  if (delta.us() >= kMaxCompactNtp * kNumMicrosecsPerSec / kCompactNtpInSecond)
    return kMaxCompactNtp;
  // To convert to compact ntp need to divide by 1e6 to get seconds,
  // then multiply by 0x10000 to get the final result.
  // To avoid float operations, multiplication and division swapped.
  return DivideRoundToNearest(delta.us() * kCompactNtpInSecond,
                              kNumMicrosecsPerSec);
}

TimeDelta CompactNtpIntervalToTimeDelta(uint32_t compact_ntp_interval) {
  // Convert to 64bit value to avoid multiplication overflow.
  int64_t value = int64_t{compact_ntp_interval};
  if (compact_ntp_interval > 0x8000'0000) {
    value -= (int64_t{1} << 32);
  }
  // To convert to TimeDelta need to divide by 2^16 to get seconds,
  // then multiply by 1'000'000 to get microseconds. To avoid float operations,
  // multiplication and division are swapped.
  int64_t us = DivideRoundToNearest(value * kNumMicrosecsPerSec, 1 << 16);
  return TimeDelta::Micros(us);
}

TimeDelta CompactNtpRttToTimeDelta(uint32_t compact_ntp_interval) {
  static constexpr TimeDelta kMinRtt = TimeDelta::Millis(1);
  // Interval to convert expected to be positive, e.g. RTT or delay.
  // Because interval can be derived from non-monotonic ntp clock,
  // it might become negative that is indistinguishable from very large values.
  // Since very large RTT/delay is less likely than non-monotonic ntp clock,
  // such value is considered negative and converted to minimum value of 1ms.
  // Small RTT value is considered too good to be true and increased to 1ms.
  return std::max(CompactNtpIntervalToTimeDelta(compact_ntp_interval), kMinRtt);
}
}  // namespace webrtc
