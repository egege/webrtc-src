/*
 *  Copyright (c) 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "api/rtp_headers.h"
#include "api/transport/rtp/corruption_detection_message.h"
#include "api/video/color_space.h"
#include "api/video/video_content_type.h"
#include "api/video/video_layers_allocation.h"
#include "api/video/video_timing.h"
#include "modules/rtp_rtcp/include/rtp_rtcp_defines.h"
#include "modules/rtp_rtcp/source/corruption_detection_extension.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor.h"
#include "modules/rtp_rtcp/source/rtp_generic_frame_descriptor_extension.h"
#include "modules/rtp_rtcp/source/rtp_header_extensions.h"
#include "modules/rtp_rtcp/source/rtp_packet_received.h"
#include "modules/rtp_rtcp/source/rtp_video_layers_allocation_extension.h"

namespace webrtc {
// We decide which header extensions to register by reading four bytes
// from the beginning of `data` and interpreting it as a bitmask over
// the RTPExtensionType enum. This assert ensures four bytes are enough.
static_assert(kRtpExtensionNumberOfExtensions <= 32,
              "Insufficient bits read to configure all header extensions. Add "
              "an extra byte and update the switches.");

void FuzzOneInput(const uint8_t* data, size_t size) {
  if (size <= 4)
    return;

  // Don't use the configuration byte as part of the packet.
  std::bitset<32> extensionMask(*reinterpret_cast<const uint32_t*>(data));
  data += 4;
  size -= 4;

  RtpPacketReceived::ExtensionManager extensions(/*extmap_allow_mixed=*/true);
  // Start at local_id = 1 since 0 is an invalid extension id.
  int local_id = 1;
  // Skip i = 0 since it maps to kRtpExtensionNone.
  for (int i = 1; i < kRtpExtensionNumberOfExtensions; i++) {
    RTPExtensionType extension_type = static_cast<RTPExtensionType>(i);
    if (extensionMask[i]) {
      // Extensions are registered with an ID, which you signal to the
      // peer so they know what to expect. This code only cares about
      // parsing so the value of the ID isn't relevant.
      extensions.RegisterByType(local_id++, extension_type);
    }
  }

  RtpPacketReceived packet(&extensions);
  packet.Parse(data, size);

  // Call packet accessors because they have extra checks.
  packet.Marker();
  packet.PayloadType();
  packet.SequenceNumber();
  packet.Timestamp();
  packet.Ssrc();
  packet.Csrcs();

  // Each extension has its own getter. It is supported behaviour to
  // call GetExtension on an extension which was not registered, so we
  // don't check the bitmask here.
  for (int i = 0; i < kRtpExtensionNumberOfExtensions; i++) {
    switch (static_cast<RTPExtensionType>(i)) {
      case kRtpExtensionNone:
      case kRtpExtensionNumberOfExtensions:
        break;
      case kRtpExtensionTransmissionTimeOffset: {
        int32_t offset;
        packet.GetExtension<TransmissionOffset>(&offset);
        break;
      }
      case kRtpExtensionAudioLevel: {
        AudioLevel audio_level;
        packet.GetExtension<AudioLevelExtension>(&audio_level);
        break;
      }
      case kRtpExtensionCsrcAudioLevel: {
        std::vector<uint8_t> audio_levels;
        packet.GetExtension<CsrcAudioLevel>(&audio_levels);
        break;
      }
      case kRtpExtensionAbsoluteSendTime:
        uint32_t sendtime;
        packet.GetExtension<AbsoluteSendTime>(&sendtime);
        break;
      case kRtpExtensionAbsoluteCaptureTime: {
        AbsoluteCaptureTime extension;
        packet.GetExtension<AbsoluteCaptureTimeExtension>(&extension);
        break;
      }
      case kRtpExtensionVideoRotation: {
        uint8_t rotation;
        packet.GetExtension<VideoOrientation>(&rotation);
        break;
      }
      case kRtpExtensionTransportSequenceNumber: {
        uint16_t seqnum;
        packet.GetExtension<TransportSequenceNumber>(&seqnum);
        break;
      }
      case kRtpExtensionTransportSequenceNumber02: {
        uint16_t seqnum;
        std::optional<FeedbackRequest> feedback_request;
        packet.GetExtension<TransportSequenceNumberV2>(&seqnum,
                                                       &feedback_request);
        break;
      }
      case kRtpExtensionPlayoutDelay: {
        VideoPlayoutDelay playout;
        packet.GetExtension<PlayoutDelayLimits>(&playout);
        break;
      }
      case kRtpExtensionVideoContentType: {
        VideoContentType content_type;
        packet.GetExtension<VideoContentTypeExtension>(&content_type);
        break;
      }
      case kRtpExtensionVideoTiming: {
        VideoSendTiming timing;
        packet.GetExtension<VideoTimingExtension>(&timing);
        break;
      }
      case kRtpExtensionRtpStreamId: {
        std::string rsid;
        packet.GetExtension<RtpStreamId>(&rsid);
        break;
      }
      case kRtpExtensionRepairedRtpStreamId: {
        std::string rsid;
        packet.GetExtension<RepairedRtpStreamId>(&rsid);
        break;
      }
      case kRtpExtensionMid: {
        std::string mid;
        packet.GetExtension<RtpMid>(&mid);
        break;
      }
      case kRtpExtensionGenericFrameDescriptor: {
        RtpGenericFrameDescriptor descriptor;
        packet.GetExtension<RtpGenericFrameDescriptorExtension00>(&descriptor);
        break;
      }
      case kRtpExtensionColorSpace: {
        ColorSpace color_space;
        packet.GetExtension<ColorSpaceExtension>(&color_space);
        break;
      }
      case kRtpExtensionInbandComfortNoise: {
        std::optional<uint8_t> noise_level;
        packet.GetExtension<InbandComfortNoiseExtension>(&noise_level);
        break;
      }
      case kRtpExtensionVideoLayersAllocation: {
        VideoLayersAllocation allocation;
        packet.GetExtension<RtpVideoLayersAllocationExtension>(&allocation);
        break;
      }
      case kRtpExtensionVideoFrameTrackingId: {
        uint16_t tracking_id;
        packet.GetExtension<VideoFrameTrackingIdExtension>(&tracking_id);
        break;
      }
      case kRtpExtensionDependencyDescriptor:
        // This extension requires state to read and so complicated that
        // deserves own fuzzer.
        break;
      case kRtpExtensionCorruptionDetection: {
        CorruptionDetectionMessage message;
        packet.GetExtension<CorruptionDetectionExtension>(&message);
        break;
      }
    }
  }

  // Check that zero-ing mutable extensions wouldn't cause any problems.
  packet.ZeroMutableExtensions();
}
}  // namespace webrtc
