/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "test/pc/e2e/stats_poller.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/strings/string_view.h"
#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "api/stats/rtc_stats_report.h"
#include "api/test/stats_observer_interface.h"
#include "rtc_base/synchronization/mutex.h"
#include "test/pc/e2e/stats_provider.h"
#include "test/pc/e2e/test_peer.h"

namespace webrtc {
namespace webrtc_pc_e2e {

void InternalStatsObserver::PollStats() {
  peer_->GetStats(this);
}

void InternalStatsObserver::OnStatsDelivered(
    const scoped_refptr<const RTCStatsReport>& report) {
  for (auto* observer : observers_) {
    observer->OnStatsReports(pc_label_, report);
  }
}

StatsPoller::StatsPoller(std::vector<StatsObserverInterface*> observers,
                         std::map<std::string, StatsProvider*> peers)
    : observers_(std::move(observers)) {
  MutexLock lock(&mutex_);
  for (auto& peer : peers) {
    pollers_.push_back(make_ref_counted<InternalStatsObserver>(
        peer.first, peer.second, observers_));
  }
}

StatsPoller::StatsPoller(std::vector<StatsObserverInterface*> observers,
                         std::map<std::string, TestPeer*> peers)
    : observers_(std::move(observers)) {
  MutexLock lock(&mutex_);
  for (auto& peer : peers) {
    pollers_.push_back(make_ref_counted<InternalStatsObserver>(
        peer.first, peer.second, observers_));
  }
}

void StatsPoller::PollStatsAndNotifyObservers() {
  MutexLock lock(&mutex_);
  for (auto& poller : pollers_) {
    poller->PollStats();
  }
}

void StatsPoller::RegisterParticipantInCall(absl::string_view peer_name,
                                            StatsProvider* peer) {
  MutexLock lock(&mutex_);
  pollers_.push_back(
      make_ref_counted<InternalStatsObserver>(peer_name, peer, observers_));
}

bool StatsPoller::UnregisterParticipantInCall(absl::string_view peer_name) {
  MutexLock lock(&mutex_);
  for (auto it = pollers_.begin(); it != pollers_.end(); ++it) {
    if ((*it)->pc_label() == peer_name) {
      pollers_.erase(it);
      return true;
    }
  }
  return false;
}

}  // namespace webrtc_pc_e2e
}  // namespace webrtc
