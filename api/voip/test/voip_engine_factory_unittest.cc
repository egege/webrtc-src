/*
 *  Copyright (c) 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/voip/voip_engine_factory.h"

#include <memory>
#include <utility>

#include "api/environment/environment_factory.h"
#include "api/make_ref_counted.h"
#include "modules/audio_device/include/mock_audio_device.h"
#include "modules/audio_processing/include/mock_audio_processing.h"
#include "test/create_test_field_trials.h"
#include "test/gmock.h"
#include "test/gtest.h"
#include "test/mock_audio_decoder_factory.h"
#include "test/mock_audio_encoder_factory.h"

namespace webrtc {
namespace {

using ::testing::NiceMock;

// Create voip engine with mock modules as normal use case.
TEST(VoipEngineFactoryTest, CreateEngineWithMockModules) {
  VoipEngineConfig config;
  config.encoder_factory = make_ref_counted<MockAudioEncoderFactory>();
  config.decoder_factory = make_ref_counted<MockAudioDecoderFactory>();
  config.env = CreateEnvironment(CreateTestFieldTrialsPtr());
  config.audio_processing_builder =
      std::make_unique<NiceMock<test::MockAudioProcessingBuilder>>();
  config.audio_device_module = test::MockAudioDeviceModule::CreateNice();

  auto voip_engine = CreateVoipEngine(std::move(config));
  EXPECT_NE(voip_engine, nullptr);
}

// Create voip engine without setting optional components.
TEST(VoipEngineFactoryTest, UseNoAudioProcessing) {
  VoipEngineConfig config;
  config.encoder_factory = make_ref_counted<MockAudioEncoderFactory>();
  config.decoder_factory = make_ref_counted<MockAudioDecoderFactory>();
  config.audio_device_module = test::MockAudioDeviceModule::CreateNice();

  auto voip_engine = CreateVoipEngine(std::move(config));
  EXPECT_NE(voip_engine, nullptr);
}

}  // namespace
}  // namespace webrtc
