/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "api/test/network_emulation/network_emulation_interfaces.h"

#include <cstdint>

#include "api/transport/ecn_marking.h"
#include "api/units/data_rate.h"
#include "api/units/timestamp.h"
#include "rtc_base/checks.h"
#include "rtc_base/copy_on_write_buffer.h"
#include "rtc_base/net_helper.h"
#include "rtc_base/net_helpers.h"
#include "rtc_base/socket_address.h"

namespace webrtc {

EmulatedIpPacket::EmulatedIpPacket(const SocketAddress& from,
                                   const SocketAddress& to,
                                   CopyOnWriteBuffer data,
                                   Timestamp arrival_time,
                                   uint16_t application_overhead,
                                   EcnMarking ecn)
    : from(from),
      to(to),
      data(data),
      headers_size(to.ipaddr().overhead() + application_overhead +
                   kUdpHeaderSize),
      arrival_time(arrival_time),
      ecn(ecn) {
  RTC_DCHECK(to.family() == AF_INET || to.family() == AF_INET6);
}

DataRate EmulatedNetworkOutgoingStats::AverageSendRate() const {
  RTC_DCHECK_GE(packets_sent, 2);
  RTC_DCHECK(first_packet_sent_time.IsFinite());
  RTC_DCHECK(last_packet_sent_time.IsFinite());
  return (bytes_sent - first_sent_packet_size) /
         (last_packet_sent_time - first_packet_sent_time);
}

DataRate EmulatedNetworkIncomingStats::AverageReceiveRate() const {
  RTC_DCHECK_GE(packets_received, 2);
  RTC_DCHECK(first_packet_received_time.IsFinite());
  RTC_DCHECK(last_packet_received_time.IsFinite());
  return (bytes_received - first_received_packet_size) /
         (last_packet_received_time - first_packet_received_time);
}

}  // namespace webrtc
