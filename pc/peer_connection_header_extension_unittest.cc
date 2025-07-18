/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/media_types.h"
#include "api/peer_connection_interface.h"
#include "api/rtc_error.h"
#include "api/rtc_event_log/rtc_event_log_factory.h"
#include "api/rtp_parameters.h"
#include "api/rtp_transceiver_direction.h"
#include "api/rtp_transceiver_interface.h"
#include "api/scoped_refptr.h"
#include "media/base/fake_media_engine.h"
#include "p2p/test/fake_port_allocator.h"
#include "pc/peer_connection_wrapper.h"
#include "pc/session_description.h"
#include "pc/test/enable_fake_media.h"
#include "pc/test/mock_peer_connection_observers.h"
#include "rtc_base/internal/default_socket_server.h"
#include "rtc_base/socket_server.h"
#include "rtc_base/strings/string_builder.h"
#include "rtc_base/thread.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace webrtc {

using ::testing::Combine;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Return;
using ::testing::Values;

class PeerConnectionHeaderExtensionTest
    : public ::testing::TestWithParam<std::tuple<MediaType, SdpSemantics>> {
 protected:
  PeerConnectionHeaderExtensionTest()
      : socket_server_(CreateDefaultSocketServer()),
        main_thread_(socket_server_.get()),
        extensions_(
            {RtpHeaderExtensionCapability("uri1",
                                          1,
                                          RtpTransceiverDirection::kStopped),
             RtpHeaderExtensionCapability("uri2",
                                          2,
                                          RtpTransceiverDirection::kSendOnly),
             RtpHeaderExtensionCapability("uri3",
                                          3,
                                          RtpTransceiverDirection::kRecvOnly),
             RtpHeaderExtensionCapability(
                 "uri4",
                 4,
                 RtpTransceiverDirection::kSendRecv)}) {}

  std::unique_ptr<PeerConnectionWrapper> CreatePeerConnection(
      MediaType media_type,
      std::optional<SdpSemantics> semantics) {
    auto media_engine = std::make_unique<FakeMediaEngine>();
    if (media_type == MediaType::AUDIO)
      media_engine->fake_voice_engine()->SetRtpHeaderExtensions(extensions_);
    else
      media_engine->fake_video_engine()->SetRtpHeaderExtensions(extensions_);
    PeerConnectionFactoryDependencies factory_dependencies;
    factory_dependencies.network_thread = Thread::Current();
    factory_dependencies.worker_thread = Thread::Current();
    factory_dependencies.signaling_thread = Thread::Current();
    EnableFakeMedia(factory_dependencies, std::move(media_engine));

    factory_dependencies.event_log_factory =
        std::make_unique<RtcEventLogFactory>();

    auto pc_factory =
        CreateModularPeerConnectionFactory(std::move(factory_dependencies));

    auto fake_port_allocator = std::make_unique<FakePortAllocator>(
        CreateEnvironment(), socket_server_.get());
    auto observer = std::make_unique<MockPeerConnectionObserver>();
    PeerConnectionInterface::RTCConfiguration config;
    if (semantics)
      config.sdp_semantics = *semantics;
    PeerConnectionDependencies pc_dependencies(observer.get());
    pc_dependencies.allocator = std::move(fake_port_allocator);
    auto result = pc_factory->CreatePeerConnectionOrError(
        config, std::move(pc_dependencies));
    EXPECT_TRUE(result.ok());
    observer->SetPeerConnectionInterface(result.value().get());
    return std::make_unique<PeerConnectionWrapper>(
        pc_factory, result.MoveValue(), std::move(observer));
  }

  std::unique_ptr<SocketServer> socket_server_;
  AutoSocketServerThread main_thread_;
  std::vector<RtpHeaderExtensionCapability> extensions_;
};

TEST_P(PeerConnectionHeaderExtensionTest, TransceiverOffersHeaderExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> wrapper =
      CreatePeerConnection(media_type, semantics);
  auto transceiver = wrapper->AddTransceiver(media_type);
  EXPECT_EQ(transceiver->GetHeaderExtensionsToNegotiate(), extensions_);
}

TEST_P(PeerConnectionHeaderExtensionTest,
       SenderReceiverCapabilitiesReturnNotStoppedExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  std::unique_ptr<PeerConnectionWrapper> wrapper =
      CreatePeerConnection(media_type, semantics);
  EXPECT_THAT(wrapper->pc_factory()
                  ->GetRtpSenderCapabilities(media_type)
                  .header_extensions,
              ElementsAre(Field(&RtpHeaderExtensionCapability::uri, "uri2"),
                          Field(&RtpHeaderExtensionCapability::uri, "uri3"),
                          Field(&RtpHeaderExtensionCapability::uri, "uri4")));
  EXPECT_EQ(wrapper->pc_factory()
                ->GetRtpReceiverCapabilities(media_type)
                .header_extensions,
            wrapper->pc_factory()
                ->GetRtpSenderCapabilities(media_type)
                .header_extensions);
}

TEST_P(PeerConnectionHeaderExtensionTest, OffersUnstoppedDefaultExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> wrapper =
      CreatePeerConnection(media_type, semantics);
  auto transceiver = wrapper->AddTransceiver(media_type);
  auto session_description = wrapper->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
}

TEST_P(PeerConnectionHeaderExtensionTest, OffersUnstoppedModifiedExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> wrapper =
      CreatePeerConnection(media_type, semantics);
  auto transceiver = wrapper->AddTransceiver(media_type);
  auto modified_extensions = transceiver->GetHeaderExtensionsToNegotiate();
  modified_extensions[0].direction = RtpTransceiverDirection::kSendRecv;
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  EXPECT_TRUE(
      transceiver->SetHeaderExtensionsToNegotiate(modified_extensions).ok());
  auto session_description = wrapper->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri1"),
                          Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
}

TEST_P(PeerConnectionHeaderExtensionTest, AnswersUnstoppedModifiedExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc2->SetRemoteDescription(std::move(offer));

  ASSERT_EQ(pc2->pc()->GetTransceivers().size(), 1u);
  auto transceiver2 = pc2->pc()->GetTransceivers()[0];
  auto modified_extensions = transceiver2->GetHeaderExtensionsToNegotiate();
  // Don't offer uri4.
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver2->SetHeaderExtensionsToNegotiate(modified_extensions);

  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  EXPECT_THAT(answer->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
}

TEST_P(PeerConnectionHeaderExtensionTest, NegotiatedExtensionsAreAccessible) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);
  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);
  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());

  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver2 = pc2->AddTransceiver(media_type);
  pc2->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc1->SetRemoteDescription(std::move(answer));

  // PC1 has exts 2-4 unstopped and PC2 has exts 1-3 unstopped -> ext 2, 3
  // survives.
  EXPECT_THAT(transceiver1->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kStopped)));
}

TEST_P(PeerConnectionHeaderExtensionTest, OfferedExtensionsArePerTransceiver) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);
  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);
  auto transceiver2 = pc1->AddTransceiver(media_type);

  auto session_description = pc1->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
  EXPECT_THAT(session_description->description()
                  ->contents()[1]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
}

TEST_P(PeerConnectionHeaderExtensionTest, RemovalAfterRenegotiation) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc2->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc1->SetRemoteDescription(std::move(answer));

  auto modified_extensions = transceiver1->GetHeaderExtensionsToNegotiate();
  modified_extensions[3].direction = RtpTransceiverDirection::kStopped;
  transceiver1->SetHeaderExtensionsToNegotiate(modified_extensions);
  auto session_description = pc1->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3")));
}

TEST_P(PeerConnectionHeaderExtensionTest,
       StoppedByDefaultExtensionCanBeActivatedByRemoteSdp) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc2->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  std::string sdp;
  ASSERT_TRUE(answer->ToString(&sdp));
  // We support uri1 but it is stopped by default. Let the remote reactivate it.
  sdp += "a=extmap:15 uri1\r\n";
  auto modified_answer = CreateSessionDescription(SdpType::kAnswer, sdp);
  pc1->SetRemoteDescription(std::move(modified_answer));
  EXPECT_THAT(transceiver1->GetNegotiatedHeaderExtensions(),
              ElementsAre(Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv),
                          Field(&RtpHeaderExtensionCapability::direction,
                                RtpTransceiverDirection::kSendRecv)));
}

TEST_P(PeerConnectionHeaderExtensionTest,
       UnknownExtensionInRemoteOfferDoesNotShowUp) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> pc =
      CreatePeerConnection(media_type, semantics);
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=fingerprint:sha-256 "
      "A7:24:72:CA:6E:02:55:39:BA:66:DF:6E:CC:4C:D8:B0:1A:BF:1A:56:65:7D:F4:03:"
      "AD:7E:77:43:2A:29:EC:93\r\n"
      "a=ice-ufrag:6HHHdzzeIhkE0CKj\r\n"
      "a=ice-pwd:XYDGVpfvklQIEnZ6YnyLsAew\r\n";
  if (media_type == MediaType::AUDIO) {
    sdp +=
        "m=audio 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_audio_codec/8000\r\n";
  } else {
    sdp +=
        "m=video 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_video_codec/90000\r\n";
  }
  sdp +=
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendonly\r\n"
      "a=mid:audio\r\n"
      "a=setup:actpass\r\n"
      "a=extmap:1 urn:bogus\r\n";
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateSessionDescription(SdpType::kOffer, sdp);
  pc->SetRemoteDescription(std::move(offer));
  pc->CreateAnswerAndSetAsLocal(
      PeerConnectionInterface::RTCOfferAnswerOptions());
  ASSERT_GT(pc->pc()->GetTransceivers().size(), 0u);
  auto transceiver = pc->pc()->GetTransceivers()[0];
  auto negotiated = transceiver->GetNegotiatedHeaderExtensions();
  EXPECT_EQ(negotiated.size(),
            transceiver->GetHeaderExtensionsToNegotiate().size());
  // All extensions are stopped, the "bogus" one does not show up.
  for (const auto& extension : negotiated) {
    EXPECT_EQ(extension.direction, RtpTransceiverDirection::kStopped);
    EXPECT_NE(extension.uri, "urn:bogus");
  }
}

// These tests are regression tests for behavior that the API
// enables in a proper way. It conflicts with the behavior
// of the API to only offer non-stopped extensions.
TEST_P(PeerConnectionHeaderExtensionTest,
       SdpMungingAnswerWithoutApiUsageEnablesExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> pc =
      CreatePeerConnection(media_type, semantics);
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=fingerprint:sha-256 "
      "A7:24:72:CA:6E:02:55:39:BA:66:DF:6E:CC:4C:D8:B0:1A:BF:1A:56:65:7D:F4:03:"
      "AD:7E:77:43:2A:29:EC:93\r\n"
      "a=ice-ufrag:6HHHdzzeIhkE0CKj\r\n"
      "a=ice-pwd:XYDGVpfvklQIEnZ6YnyLsAew\r\n";
  if (media_type == MediaType::AUDIO) {
    sdp +=
        "m=audio 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_audio_codec/8000\r\n";
  } else {
    sdp +=
        "m=video 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_video_codec/90000\r\n";
  }
  sdp +=
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendrecv\r\n"
      "a=mid:audio\r\n"
      "a=setup:actpass\r\n"
      "a=extmap:1 uri1\r\n";
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateSessionDescription(SdpType::kOffer, sdp);
  pc->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc->CreateAnswer(PeerConnectionInterface::RTCOfferAnswerOptions());
  std::string modified_sdp;
  ASSERT_TRUE(answer->ToString(&modified_sdp));
  modified_sdp += "a=extmap:1 uri1\r\n";
  auto modified_answer =
      CreateSessionDescription(SdpType::kAnswer, modified_sdp);
  ASSERT_TRUE(pc->SetLocalDescription(std::move(modified_answer)));

  auto session_description = pc->CreateOffer();
  EXPECT_THAT(session_description->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri1"),
                          Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4")));
}

TEST_P(PeerConnectionHeaderExtensionTest,
       SdpMungingOfferWithoutApiUsageEnablesExtensions) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> pc =
      CreatePeerConnection(media_type, semantics);
  pc->AddTransceiver(media_type);

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc->CreateOffer(PeerConnectionInterface::RTCOfferAnswerOptions());
  std::string modified_sdp;
  ASSERT_TRUE(offer->ToString(&modified_sdp));
  modified_sdp += "a=extmap:1 uri1\r\n";
  auto modified_offer = CreateSessionDescription(SdpType::kOffer, modified_sdp);
  ASSERT_TRUE(pc->SetLocalDescription(std::move(modified_offer)));

  auto offer2 =
      pc->CreateOffer(PeerConnectionInterface::RTCOfferAnswerOptions());
  EXPECT_THAT(offer2->description()
                  ->contents()[0]
                  .media_description()
                  ->rtp_header_extensions(),
              ElementsAre(Field(&RtpExtension::uri, "uri2"),
                          Field(&RtpExtension::uri, "uri3"),
                          Field(&RtpExtension::uri, "uri4"),
                          Field(&RtpExtension::uri, "uri1")));
}

TEST_P(PeerConnectionHeaderExtensionTest, EnablingExtensionsAfterRemoteOffer) {
  MediaType media_type;
  SdpSemantics semantics;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan)
    return;
  std::unique_ptr<PeerConnectionWrapper> pc =
      CreatePeerConnection(media_type, semantics);
  std::string sdp =
      "v=0\r\n"
      "o=- 0 3 IN IP4 127.0.0.1\r\n"
      "s=-\r\n"
      "t=0 0\r\n"
      "a=fingerprint:sha-256 "
      "A7:24:72:CA:6E:02:55:39:BA:66:DF:6E:CC:4C:D8:B0:1A:BF:1A:56:65:7D:F4:03:"
      "AD:7E:77:43:2A:29:EC:93\r\n"
      "a=ice-ufrag:6HHHdzzeIhkE0CKj\r\n"
      "a=ice-pwd:XYDGVpfvklQIEnZ6YnyLsAew\r\n";
  if (media_type == MediaType::AUDIO) {
    sdp +=
        "m=audio 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_audio_codec/8000\r\n";
  } else {
    sdp +=
        "m=video 9 RTP/AVPF 111\r\n"
        "a=rtpmap:111 fake_video_codec/90000\r\n";
  }
  sdp +=
      "c=IN IP4 0.0.0.0\r\n"
      "a=rtcp-mux\r\n"
      "a=sendrecv\r\n"
      "a=mid:audio\r\n"
      "a=setup:actpass\r\n"
      "a=extmap:5 uri1\r\n";
  std::unique_ptr<SessionDescriptionInterface> offer =
      CreateSessionDescription(SdpType::kOffer, sdp);
  pc->SetRemoteDescription(std::move(offer));

  ASSERT_GT(pc->pc()->GetTransceivers().size(), 0u);
  auto transceiver = pc->pc()->GetTransceivers()[0];
  auto modified_extensions = transceiver->GetHeaderExtensionsToNegotiate();
  modified_extensions[0].direction = RtpTransceiverDirection::kSendRecv;
  transceiver->SetHeaderExtensionsToNegotiate(modified_extensions);

  pc->CreateAnswerAndSetAsLocal(
      PeerConnectionInterface::RTCOfferAnswerOptions());

  auto session_description = pc->CreateOffer();
  auto extensions = session_description->description()
                        ->contents()[0]
                        .media_description()
                        ->rtp_header_extensions();
  EXPECT_THAT(extensions, ElementsAre(Field(&RtpExtension::uri, "uri1"),
                                      Field(&RtpExtension::uri, "uri2"),
                                      Field(&RtpExtension::uri, "uri3"),
                                      Field(&RtpExtension::uri, "uri4")));
  // Check uri1's id still matches the remote id.
  EXPECT_EQ(extensions[0].id, 5);
}

TEST_P(PeerConnectionHeaderExtensionTest, SenderParametersReflectNegotiation) {
  SdpSemantics semantics;
  MediaType media_type;
  std::tie(media_type, semantics) = GetParam();
  if (semantics != SdpSemantics::kUnifiedPlan) {
    GTEST_SKIP() << "This test only works with Unified Plan";
  }
  std::unique_ptr<PeerConnectionWrapper> pc1 =
      CreatePeerConnection(media_type, semantics);
  std::unique_ptr<PeerConnectionWrapper> pc2 =
      CreatePeerConnection(media_type, semantics);
  auto transceiver1 = pc1->AddTransceiver(media_type);
  // Before connection, sender sender_parameters should be empty.
  {
    auto sender_parameters = pc1->pc()->GetSenders()[0]->GetParameters();
    EXPECT_THAT(sender_parameters.header_extensions, IsEmpty());
  }

  std::unique_ptr<SessionDescriptionInterface> offer =
      pc1->CreateOfferAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc2->SetRemoteDescription(std::move(offer));
  std::unique_ptr<SessionDescriptionInterface> answer =
      pc2->CreateAnswerAndSetAsLocal(
          PeerConnectionInterface::RTCOfferAnswerOptions());
  pc1->SetRemoteDescription(std::move(answer));
  {
    auto sender_parameters = pc1->pc()->GetSenders()[0]->GetParameters();
    // We expect to see all send or sendrecv extensions from the answer.
    EXPECT_THAT(sender_parameters.header_extensions,
                UnorderedElementsAre(Field(&RtpExtension::uri, "uri4"),
                                     Field(&RtpExtension::uri, "uri2"),
                                     Field(&RtpExtension::uri, "uri3")));
  }
}

INSTANTIATE_TEST_SUITE_P(
    ,
    PeerConnectionHeaderExtensionTest,
    Combine(Values(SdpSemantics::kPlanB_DEPRECATED, SdpSemantics::kUnifiedPlan),
            Values(MediaType::AUDIO, MediaType::VIDEO)),
    [](const testing::TestParamInfo<
        PeerConnectionHeaderExtensionTest::ParamType>& info) {
      MediaType media_type;
      SdpSemantics semantics;
      std::tie(media_type, semantics) = info.param;
      return (StringBuilder("With")
              << (semantics == SdpSemantics::kPlanB_DEPRECATED ? "PlanB"
                                                               : "UnifiedPlan")
              << "And" << (media_type == MediaType::AUDIO ? "Voice" : "Video")
              << "Engine")
          .str();
    });

}  // namespace webrtc
