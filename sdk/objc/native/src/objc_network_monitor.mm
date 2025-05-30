/*
 *  Copyright 2020 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "sdk/objc/native/src/objc_network_monitor.h"
#include "absl/strings/string_view.h"

#include <algorithm>

#include "rtc_base/logging.h"
#include "rtc_base/string_utils.h"

namespace webrtc {

webrtc::NetworkMonitorInterface*
    ObjCNetworkMonitorFactory::CreateNetworkMonitor(
        const FieldTrialsView& field_trials) {
  return new ObjCNetworkMonitor();
}

ObjCNetworkMonitor::ObjCNetworkMonitor() {
  safety_flag_ = PendingTaskSafetyFlag::Create();
}

ObjCNetworkMonitor::~ObjCNetworkMonitor() {
  [network_monitor_ stop];
  network_monitor_ = nil;
}

void ObjCNetworkMonitor::Start() {
  if (started_) {
    return;
  }
  thread_ = webrtc::Thread::Current();
  RTC_DCHECK_RUN_ON(thread_);
  safety_flag_->SetAlive();
  network_monitor_ = [[RTCNetworkMonitor alloc] initWithObserver:this];
  if (network_monitor_ == nil) {
    RTC_LOG(LS_WARNING)
        << "Failed to create RTCNetworkMonitor; not available on this OS?";
  }
  started_ = true;
}

void ObjCNetworkMonitor::Stop() {
  RTC_DCHECK_RUN_ON(thread_);
  if (!started_) {
    return;
  }
  safety_flag_->SetNotAlive();
  [network_monitor_ stop];
  network_monitor_ = nil;
  started_ = false;
}

webrtc::NetworkMonitorInterface::InterfaceInfo
    ObjCNetworkMonitor::GetInterfaceInfo(absl::string_view interface_name) {
  RTC_DCHECK_RUN_ON(thread_);
  if (adapter_type_by_name_.empty()) {
    // If we have no path update, assume everything's available, because it's
    // preferable for WebRTC to try all interfaces rather than none at all.
    return {
        .adapter_type = webrtc::ADAPTER_TYPE_UNKNOWN,
        .available = true,
    };
  }
  auto iter = adapter_type_by_name_.find(interface_name);
  if (iter == adapter_type_by_name_.end()) {
    return {
        .adapter_type = webrtc::ADAPTER_TYPE_UNKNOWN,
        .available = false,
    };
  }

  return {
      .adapter_type = iter->second,
      .available = true,
  };
}

void ObjCNetworkMonitor::OnPathUpdate(
    std::map<std::string, webrtc::AdapterType, webrtc::AbslStringViewCmp>
        adapter_type_by_name) {
  thread_->PostTask(SafeTask(safety_flag_, [this, adapter_type_by_name] {
    RTC_DCHECK_RUN_ON(thread_);
    RTC_DCHECK(network_monitor_ != nil);
    adapter_type_by_name_ = adapter_type_by_name;
    InvokeNetworksChangedCallback();
  }));
}

}  // namespace webrtc
