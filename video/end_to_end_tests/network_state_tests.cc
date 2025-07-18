/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <cstdint>
#include <memory>
#include <vector>

#include "api/array_view.h"
#include "api/call/transport.h"
#include "api/environment/environment.h"
#include "api/media_types.h"
#include "api/rtp_headers.h"
#include "api/task_queue/task_queue_base.h"
#include "api/task_queue/task_queue_factory.h"
#include "api/test/simulated_network.h"
#include "api/units/time_delta.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_type.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "call/call.h"
#include "call/fake_network_pipe.h"
#include "call/video_receive_stream.h"
#include "call/video_send_stream.h"
#include "modules/rtp_rtcp/source/rtp_packet.h"
#include "rtc_base/event.h"
#include "rtc_base/synchronization/mutex.h"
#include "rtc_base/task_queue_for_test.h"
#include "rtc_base/thread.h"
#include "rtc_base/thread_annotations.h"
#include "test/call_test.h"
#include "test/fake_encoder.h"
#include "test/gtest.h"
#include "test/rtp_rtcp_observer.h"
#include "test/video_encoder_proxy_factory.h"
#include "test/video_test_constants.h"
#include "video/config/video_encoder_config.h"

namespace webrtc {
namespace {
constexpr int kSilenceTimeoutMs = 2000;
}  // namespace

class NetworkStateEndToEndTest : public test::CallTest {
 protected:
  class UnusedTransport : public Transport {
   private:
    bool SendRtp(ArrayView<const uint8_t> packet,
                 const PacketOptions& options) override {
      ADD_FAILURE() << "Unexpected RTP sent.";
      return false;
    }

    bool SendRtcp(ArrayView<const uint8_t> packet,
                  const PacketOptions& /* options */) override {
      ADD_FAILURE() << "Unexpected RTCP sent.";
      return false;
    }
  };
  class RequiredTransport : public Transport {
   public:
    RequiredTransport(bool rtp_required, bool rtcp_required)
        : need_rtp_(rtp_required), need_rtcp_(rtcp_required) {}
    ~RequiredTransport() override {
      if (need_rtp_) {
        ADD_FAILURE() << "Expected RTP packet not sent.";
      }
      if (need_rtcp_) {
        ADD_FAILURE() << "Expected RTCP packet not sent.";
      }
    }

   private:
    bool SendRtp(ArrayView<const uint8_t> packet,
                 const PacketOptions& options) override {
      MutexLock lock(&mutex_);
      need_rtp_ = false;
      return true;
    }

    bool SendRtcp(ArrayView<const uint8_t> packet,
                  const PacketOptions& /* options */) override {
      MutexLock lock(&mutex_);
      need_rtcp_ = false;
      return true;
    }
    bool need_rtp_;
    bool need_rtcp_;
    Mutex mutex_;
  };
  void VerifyNewVideoSendStreamsRespectNetworkState(
      MediaType network_to_bring_up,
      VideoEncoder* encoder,
      Transport* transport);
  void VerifyNewVideoReceiveStreamsRespectNetworkState(
      MediaType network_to_bring_up,
      Transport* transport);
};

void NetworkStateEndToEndTest::VerifyNewVideoSendStreamsRespectNetworkState(
    MediaType network_to_bring_up,
    VideoEncoder* encoder,
    Transport* transport) {
  test::VideoEncoderProxyFactory encoder_factory(encoder);

  SendTask(task_queue(), [this, network_to_bring_up, &encoder_factory,
                          transport]() {
    CreateSenderCall();
    sender_call_->SignalChannelNetworkState(network_to_bring_up, kNetworkUp);

    CreateSendConfig(1, 0, 0, transport);
    GetVideoSendConfig()->encoder_settings.encoder_factory = &encoder_factory;
    CreateVideoStreams();
    CreateFrameGeneratorCapturer(test::VideoTestConstants::kDefaultFramerate,
                                 test::VideoTestConstants::kDefaultWidth,
                                 test::VideoTestConstants::kDefaultHeight);

    Start();
  });

  Thread::SleepMs(kSilenceTimeoutMs);

  SendTask(task_queue(), [this]() {
    Stop();
    DestroyStreams();
    DestroyCalls();
  });
}

void NetworkStateEndToEndTest::VerifyNewVideoReceiveStreamsRespectNetworkState(
    MediaType network_to_bring_up,
    Transport* transport) {
  SendTask(task_queue(), [this, network_to_bring_up, transport]() {
    CreateCalls();
    receiver_call_->SignalChannelNetworkState(network_to_bring_up, kNetworkUp);
    CreateSendTransport(BuiltInNetworkBehaviorConfig(),
                        /*observer=*/nullptr);

    CreateSendConfig(1, 0, 0);
    CreateMatchingReceiveConfigs(transport);
    CreateVideoStreams();
    CreateFrameGeneratorCapturer(test::VideoTestConstants::kDefaultFramerate,
                                 test::VideoTestConstants::kDefaultWidth,
                                 test::VideoTestConstants::kDefaultHeight);
    Start();
  });

  Thread::SleepMs(kSilenceTimeoutMs);

  SendTask(task_queue(), [this]() {
    Stop();
    DestroyStreams();
    DestroyCalls();
  });
}

TEST_F(NetworkStateEndToEndTest, RespectsNetworkState) {
  // TODO(pbos): Remove accepted downtime packets etc. when signaling network
  // down blocks until no more packets will be sent.

  // Pacer will send from its packet list and then send required padding before
  // checking paused_ again. This should be enough for one round of pacing,
  // otherwise increase.
  static const int kNumAcceptedDowntimeRtp = 5;
  // A single RTCP may be in the pipeline.
  static const int kNumAcceptedDowntimeRtcp = 1;
  class NetworkStateTest : public test::EndToEndTest, public test::FakeEncoder {
   public:
    explicit NetworkStateTest(const Environment& env, TaskQueueBase* task_queue)
        : EndToEndTest(test::VideoTestConstants::kDefaultTimeout),
          FakeEncoder(env),
          e2e_test_task_queue_(task_queue),
          task_queue_(env.task_queue_factory().CreateTaskQueue(
              "NetworkStateTest",
              TaskQueueFactory::Priority::NORMAL)),
          sender_call_(nullptr),
          receiver_call_(nullptr),
          encoder_factory_(this),
          sender_state_(kNetworkUp),
          sender_rtp_(0),
          sender_padding_(0),
          sender_rtcp_(0),
          receiver_rtcp_(0),
          down_frames_(0) {}

    Action OnSendRtp(ArrayView<const uint8_t> packet) override {
      MutexLock lock(&test_mutex_);
      RtpPacket rtp_packet;
      EXPECT_TRUE(rtp_packet.Parse(packet));
      if (rtp_packet.payload_size() == 0)
        ++sender_padding_;
      ++sender_rtp_;
      packet_event_.Set();
      return SEND_PACKET;
    }

    Action OnSendRtcp(ArrayView<const uint8_t> packet) override {
      MutexLock lock(&test_mutex_);
      ++sender_rtcp_;
      packet_event_.Set();
      return SEND_PACKET;
    }

    Action OnReceiveRtp(ArrayView<const uint8_t> packet) override {
      ADD_FAILURE() << "Unexpected receiver RTP, should not be sending.";
      return SEND_PACKET;
    }

    Action OnReceiveRtcp(ArrayView<const uint8_t> packet) override {
      MutexLock lock(&test_mutex_);
      ++receiver_rtcp_;
      packet_event_.Set();
      return SEND_PACKET;
    }

    void OnCallsCreated(Call* sender_call, Call* receiver_call) override {
      sender_call_ = sender_call;
      receiver_call_ = receiver_call;
    }

    void ModifyVideoConfigs(
        VideoSendStream::Config* send_config,
        std::vector<VideoReceiveStreamInterface::Config>* receive_configs,
        VideoEncoderConfig* encoder_config) override {
      send_config->encoder_settings.encoder_factory = &encoder_factory_;
    }

    void SignalChannelNetworkState(Call* call,
                                   MediaType media_type,
                                   NetworkState network_state) {
      SendTask(e2e_test_task_queue_, [call, media_type, network_state] {
        call->SignalChannelNetworkState(media_type, network_state);
      });
    }

    void PerformTest() override {
      EXPECT_TRUE(
          encoded_frames_.Wait(test::VideoTestConstants::kDefaultTimeout))
          << "No frames received by the encoder.";

      SendTask(task_queue_.get(), [this]() {
        // Wait for packets from both sender/receiver.
        WaitForPacketsOrSilence(false, false);

        // Sender-side network down for audio; there should be no effect on
        // video
        SignalChannelNetworkState(sender_call_, MediaType::AUDIO, kNetworkDown);

        WaitForPacketsOrSilence(false, false);

        // Receiver-side network down for audio; no change expected
        SignalChannelNetworkState(receiver_call_, MediaType::AUDIO,
                                  kNetworkDown);
        WaitForPacketsOrSilence(false, false);

        // Sender-side network down.
        SignalChannelNetworkState(sender_call_, MediaType::VIDEO, kNetworkDown);
        {
          MutexLock lock(&test_mutex_);
          // After network goes down we shouldn't be encoding more frames.
          sender_state_ = kNetworkDown;
        }
        // Wait for receiver-packets and no sender packets.
        WaitForPacketsOrSilence(true, false);

        // Receiver-side network down.
        SignalChannelNetworkState(receiver_call_, MediaType::VIDEO,
                                  kNetworkDown);
        WaitForPacketsOrSilence(true, true);

        // Network up for audio for both sides; video is still not expected to
        // start
        SignalChannelNetworkState(sender_call_, MediaType::AUDIO, kNetworkUp);
        SignalChannelNetworkState(receiver_call_, MediaType::AUDIO, kNetworkUp);
        WaitForPacketsOrSilence(true, true);

        // Network back up again for both.
        {
          MutexLock lock(&test_mutex_);
          // It's OK to encode frames again, as we're about to bring up the
          // network.
          sender_state_ = kNetworkUp;
        }
        SignalChannelNetworkState(sender_call_, MediaType::VIDEO, kNetworkUp);
        SignalChannelNetworkState(receiver_call_, MediaType::VIDEO, kNetworkUp);
        WaitForPacketsOrSilence(false, false);

        // TODO(skvlad): add tests to verify that the audio streams are stopped
        // when the network goes down for audio once the workaround in
        // paced_sender.cc is removed.
      });
    }

    int32_t Encode(const VideoFrame& input_image,
                   const std::vector<VideoFrameType>* frame_types) override {
      {
        MutexLock lock(&test_mutex_);
        if (sender_state_ == kNetworkDown) {
          ++down_frames_;
          EXPECT_LE(down_frames_, 1)
              << "Encoding more than one frame while network is down.";
          if (down_frames_ > 1)
            encoded_frames_.Set();
        } else {
          encoded_frames_.Set();
        }
      }
      return test::FakeEncoder::Encode(input_image, frame_types);
    }

   private:
    void WaitForPacketsOrSilence(bool sender_down, bool receiver_down) {
      int64_t initial_time_ms = env_.clock().TimeInMilliseconds();
      int initial_sender_rtp;
      int initial_sender_rtcp;
      int initial_receiver_rtcp;
      {
        MutexLock lock(&test_mutex_);
        initial_sender_rtp = sender_rtp_;
        initial_sender_rtcp = sender_rtcp_;
        initial_receiver_rtcp = receiver_rtcp_;
      }
      bool sender_done = false;
      bool receiver_done = false;
      while (!sender_done || !receiver_done) {
        packet_event_.Wait(TimeDelta::Millis(kSilenceTimeoutMs));
        int64_t time_now_ms = env_.clock().TimeInMilliseconds();
        MutexLock lock(&test_mutex_);
        if (sender_down) {
          ASSERT_LE(sender_rtp_ - initial_sender_rtp - sender_padding_,
                    kNumAcceptedDowntimeRtp)
              << "RTP sent during sender-side downtime.";
          ASSERT_LE(sender_rtcp_ - initial_sender_rtcp,
                    kNumAcceptedDowntimeRtcp)
              << "RTCP sent during sender-side downtime.";
          if (time_now_ms - initial_time_ms >=
              static_cast<int64_t>(kSilenceTimeoutMs)) {
            sender_done = true;
          }
        } else {
          if (sender_rtp_ > initial_sender_rtp + kNumAcceptedDowntimeRtp)
            sender_done = true;
        }
        if (receiver_down) {
          ASSERT_LE(receiver_rtcp_ - initial_receiver_rtcp,
                    kNumAcceptedDowntimeRtcp)
              << "RTCP sent during receiver-side downtime.";
          if (time_now_ms - initial_time_ms >=
              static_cast<int64_t>(kSilenceTimeoutMs)) {
            receiver_done = true;
          }
        } else {
          if (receiver_rtcp_ > initial_receiver_rtcp + kNumAcceptedDowntimeRtcp)
            receiver_done = true;
        }
      }
    }

    TaskQueueBase* const e2e_test_task_queue_;
    std::unique_ptr<TaskQueueBase, TaskQueueDeleter> task_queue_;
    Mutex test_mutex_;
    Event encoded_frames_;
    Event packet_event_;
    Call* sender_call_;
    Call* receiver_call_;
    test::VideoEncoderProxyFactory encoder_factory_;
    NetworkState sender_state_ RTC_GUARDED_BY(test_mutex_);
    int sender_rtp_ RTC_GUARDED_BY(test_mutex_);
    int sender_padding_ RTC_GUARDED_BY(test_mutex_);
    int sender_rtcp_ RTC_GUARDED_BY(test_mutex_);
    int receiver_rtcp_ RTC_GUARDED_BY(test_mutex_);
    int down_frames_ RTC_GUARDED_BY(test_mutex_);
  } test(env(), task_queue());

  RunBaseTest(&test);
}

TEST_F(NetworkStateEndToEndTest, NewVideoSendStreamsRespectVideoNetworkDown) {
  class UnusedEncoder : public test::FakeEncoder {
   public:
    explicit UnusedEncoder(const Environment& env) : FakeEncoder(env) {}

    int32_t InitEncode(const VideoCodec* config,
                       const Settings& settings) override {
      EXPECT_GT(config->startBitrate, 0u);
      return 0;
    }
    int32_t Encode(const VideoFrame& input_image,
                   const std::vector<VideoFrameType>* frame_types) override {
      ADD_FAILURE() << "Unexpected frame encode.";
      return test::FakeEncoder::Encode(input_image, frame_types);
    }
  };

  UnusedEncoder unused_encoder(env());
  UnusedTransport unused_transport;
  VerifyNewVideoSendStreamsRespectNetworkState(
      MediaType::AUDIO, &unused_encoder, &unused_transport);
}

TEST_F(NetworkStateEndToEndTest, NewVideoSendStreamsIgnoreAudioNetworkDown) {
  class RequiredEncoder : public test::FakeEncoder {
   public:
    explicit RequiredEncoder(const Environment& env)
        : FakeEncoder(env), encoded_frame_(false) {}
    ~RequiredEncoder() override {
      if (!encoded_frame_) {
        ADD_FAILURE() << "Didn't encode an expected frame";
      }
    }
    int32_t Encode(const VideoFrame& input_image,
                   const std::vector<VideoFrameType>* frame_types) override {
      encoded_frame_ = true;
      return test::FakeEncoder::Encode(input_image, frame_types);
    }

   private:
    bool encoded_frame_;
  };

  RequiredTransport required_transport(true /*rtp*/, false /*rtcp*/);
  RequiredEncoder required_encoder(env());
  VerifyNewVideoSendStreamsRespectNetworkState(
      MediaType::VIDEO, &required_encoder, &required_transport);
}

TEST_F(NetworkStateEndToEndTest,
       NewVideoReceiveStreamsRespectVideoNetworkDown) {
  UnusedTransport transport;
  VerifyNewVideoReceiveStreamsRespectNetworkState(MediaType::AUDIO, &transport);
}

TEST_F(NetworkStateEndToEndTest, NewVideoReceiveStreamsIgnoreAudioNetworkDown) {
  RequiredTransport transport(false /*rtp*/, true /*rtcp*/);
  VerifyNewVideoReceiveStreamsRespectNetworkState(MediaType::VIDEO, &transport);
}

}  // namespace webrtc
