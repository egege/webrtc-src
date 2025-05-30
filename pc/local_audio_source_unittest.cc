/*
 *  Copyright 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "pc/local_audio_source.h"

#include <optional>

#include "api/audio_options.h"
#include "api/scoped_refptr.h"
#include "test/gtest.h"

using webrtc::LocalAudioSource;

TEST(LocalAudioSourceTest, InitWithAudioOptions) {
  webrtc::AudioOptions audio_options;
  audio_options.highpass_filter = true;
  webrtc::scoped_refptr<LocalAudioSource> source =
      LocalAudioSource::Create(&audio_options);
  EXPECT_EQ(true, source->options().highpass_filter);
}

TEST(LocalAudioSourceTest, InitWithNoOptions) {
  webrtc::scoped_refptr<LocalAudioSource> source =
      LocalAudioSource::Create(nullptr);
  EXPECT_EQ(std::nullopt, source->options().highpass_filter);
}
