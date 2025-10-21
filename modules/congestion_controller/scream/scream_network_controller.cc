/*
 *  Copyright (c) 2025 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/congestion_controller/scream/scream_network_controller.h"

#include <optional>
#include <utility>

#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/scream/scream_v2.h"
#include "rtc_base/logging.h"
namespace webrtc {

static constexpr DataRate kDefaultStartRate = DataRate::KilobitsPerSec(300);

ScreamNetworkController::ScreamNetworkController(NetworkControllerConfig config)
    : env_(config.env),
      scream_(std::in_place, env_),
      target_rate_constraints_(config.constraints) {
  if (config.constraints.min_data_rate.has_value() ||
      config.constraints.max_data_rate.has_value()) {
    scream_->SetTargetBitrateConstraints(
        config.constraints.min_data_rate.value_or(DataRate::Zero()),
        config.constraints.max_data_rate.value_or(DataRate::PlusInfinity()));
  }
}

NetworkControlUpdate ScreamNetworkController::OnNetworkAvailability(
    NetworkAvailability msg) {
  RTC_LOG(LS_INFO) << " OnNetworkAvailability network_available:"
                   << msg.network_available;
  if (msg.network_available) {
    // TODO: bugs.webrtc.org/447037083 - rtt must currently be set on every
    // update. But here it is not yet known.
    return CreateUpdate(
        msg.at_time,
        target_rate_constraints_.starting_rate.value_or(kDefaultStartRate),
        /*rtt=*/TimeDelta::Zero());
  }
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnNetworkRouteChange(
    NetworkRouteChange msg) {
  RTC_LOG(LS_INFO) << " OnNetworkRouteChange, resetting ScreamV2.";
  target_rate_constraints_ = msg.constraints;
  scream_.emplace(env_);
  // TODO: bugs.webrtc.org/447037083 - We should use the minimum rate from
  // constraints, REMB and remote network state estimates.
  scream_->SetTargetBitrateConstraints(
      target_rate_constraints_.min_data_rate.value_or(DataRate::Zero()),
      target_rate_constraints_.max_data_rate.value_or(
          DataRate::PlusInfinity()));
  // TODO: bugs.webrtc.org/447037083 - rtt must currently be set on every
  // update. But here it is not yet known.
  return CreateUpdate(
      msg.at_time,
      target_rate_constraints_.starting_rate.value_or(kDefaultStartRate),
      /*rtt=*/TimeDelta::Zero());
}

NetworkControlUpdate ScreamNetworkController::OnProcessInterval(
    ProcessInterval msg) {
  // Scream currently has no need for periodic processing.
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnRemoteBitrateReport(
    RemoteBitrateReport msg) {
  // TODO: bugs.webrtc.org/447037083 - Implement;
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnRoundTripTimeUpdate(
    RoundTripTimeUpdate msg) {
  // Scream uses Smoothed RTT from TransportFeedback.
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnSentPacket(SentPacket msg) {
  // Scream does not have to know about sent packets.
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnReceivedPacket(
    ReceivedPacket msg) {
  // Scream does not have to know about received packets.
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnStreamsConfig(
    StreamsConfig msg) {
  // TODO: bugs.webrtc.org/447037083 - Implement;
  RTC_LOG_F(LS_INFO) << "Not implemented";
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnTargetRateConstraints(
    TargetRateConstraints msg) {
  target_rate_constraints_ = msg;

  // TODO: bugs.webrtc.org/447037083 - We should use the minimum rate from
  // constraints, REMB and remote network state estimates.
  scream_->SetTargetBitrateConstraints(
      target_rate_constraints_.min_data_rate.value_or(DataRate::Zero()),
      target_rate_constraints_.max_data_rate.value_or(
          DataRate::PlusInfinity()));
  // No need to change target rate immediately. Wait until next feedback.
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnTransportLossReport(
    TransportLossReport msg) {
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnNetworkStateEstimate(
    NetworkStateEstimate msg) {
  // TODO: bugs.webrtc.org/447037083 - Implement;
  RTC_LOG_F(LS_INFO) << "Not implemented";
  return NetworkControlUpdate();
}

NetworkControlUpdate ScreamNetworkController::OnTransportPacketsFeedback(
    TransportPacketsFeedback msg) {
  DataRate target_rate = scream_->OnTransportPacketsFeedback(msg);
  return CreateUpdate(msg.feedback_time, target_rate, msg.smoothed_rtt);
}

NetworkControlUpdate ScreamNetworkController::CreateUpdate(Timestamp now,
                                                           DataRate target_rate,
                                                           TimeDelta rtt) {
  TargetTransferRate target_rate_msg;
  target_rate_msg.at_time = now;
  target_rate_msg.target_rate = target_rate;

  target_rate_msg.network_estimate.at_time = now;
  target_rate_msg.network_estimate.round_trip_time = rtt;
  // TODO: bugs.webrtc.org/447037083 - bwe_period must currently be set but
  // it seems like it is not used for anything sensible. Try to remove it.
  target_rate_msg.network_estimate.bwe_period = TimeDelta::Millis(25);

  NetworkControlUpdate update;
  update.target_rate = target_rate_msg;
  update.pacer_config = GetPacerConfig(target_rate);

  return update;
}

PacerConfig ScreamNetworkController::GetPacerConfig(
    DataRate target_rate) const {
  const double kPacingRateFactor = 1.5;
  // TODO: bugs.webrtc.org/447037083 - Currently, pacer will allow sending
  // bursts of packets with a total size up to 40ms * pacing rate.
  return PacerConfig::Create(env_.clock().CurrentTime(),
                             target_rate * kPacingRateFactor,
                             /*pad_rate=*/DataRate::Zero());
}

}  // namespace webrtc
