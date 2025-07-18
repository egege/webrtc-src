/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "modules/congestion_controller/goog_cc/test/goog_cc_printer.h"

#include <math.h>

#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "absl/strings/string_view.h"
#include "api/rtc_event_log_output.h"
#include "api/transport/goog_cc_factory.h"
#include "api/transport/network_control.h"
#include "api/transport/network_types.h"
#include "api/units/data_rate.h"
#include "api/units/data_size.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "modules/congestion_controller/goog_cc/alr_detector.h"
#include "modules/congestion_controller/goog_cc/delay_based_bwe.h"
#include "modules/congestion_controller/goog_cc/goog_cc_network_control.h"
#include "modules/congestion_controller/goog_cc/trendline_estimator.h"
#include "modules/remote_bitrate_estimator/aimd_rate_control.h"
#include "rtc_base/checks.h"
#include "test/logging/log_writer.h"

namespace webrtc {
namespace {
void WriteTypedValue(RtcEventLogOutput* out, int value) {
  LogWriteFormat(out, "%i", value);
}
void WriteTypedValue(RtcEventLogOutput* out, double value) {
  LogWriteFormat(out, "%.6f", value);
}
void WriteTypedValue(RtcEventLogOutput* out, std::optional<DataRate> value) {
  LogWriteFormat(out, "%.0f", value ? value->bytes_per_sec<double>() : NAN);
}
void WriteTypedValue(RtcEventLogOutput* out, std::optional<DataSize> value) {
  LogWriteFormat(out, "%.0f", value ? value->bytes<double>() : NAN);
}
void WriteTypedValue(RtcEventLogOutput* out, std::optional<TimeDelta> value) {
  LogWriteFormat(out, "%.3f", value ? value->seconds<double>() : NAN);
}
void WriteTypedValue(RtcEventLogOutput* out, std::optional<Timestamp> value) {
  LogWriteFormat(out, "%.3f", value ? value->seconds<double>() : NAN);
}

template <typename F>
class TypedFieldLogger : public FieldLogger {
 public:
  TypedFieldLogger(absl::string_view name, F&& getter)
      : name_(name), getter_(std::forward<F>(getter)) {}
  const std::string& name() const override { return name_; }
  void WriteValue(RtcEventLogOutput* out) override {
    WriteTypedValue(out, getter_());
  }

 private:
  std::string name_;
  F getter_;
};

template <typename F>
FieldLogger* Log(absl::string_view name, F&& getter) {
  return new TypedFieldLogger<F>(name, std::forward<F>(getter));
}

}  // namespace
GoogCcStatePrinter::GoogCcStatePrinter() {
  for (auto* logger : CreateLoggers()) {
    loggers_.emplace_back(logger);
  }
}

std::deque<FieldLogger*> GoogCcStatePrinter::CreateLoggers() {
  auto stable_estimate = [this] {
    return DataRate::KilobitsPerSec(
        controller_->delay_based_bwe_->rate_control_.link_capacity_
            .estimate_kbps_.value_or(-INFINITY));
  };
  auto rate_control_state = [this] {
    return static_cast<int>(
        controller_->delay_based_bwe_->rate_control_.rate_control_state_);
  };
  auto trend = [this] {
    return reinterpret_cast<TrendlineEstimator*>(
        controller_->delay_based_bwe_->active_delay_detector_);
  };
  auto acknowledged_rate = [this] {
    return controller_->acknowledged_bitrate_estimator_->bitrate();
  };
  std::deque<FieldLogger*> loggers({
      Log("time", [this] { return target_.at_time; }),
      Log("rtt", [this] { return target_.network_estimate.round_trip_time; }),
      Log("target", [this] { return target_.target_rate; }),
      Log("pacing", [this] { return pacing_.data_rate(); }),
      Log("padding", [this] { return pacing_.pad_rate(); }),
      Log("window", [this] { return congestion_window_; }),
      Log("rate_control_state", [=] { return rate_control_state(); }),
      Log("stable_estimate", [=] { return stable_estimate(); }),
      Log("trendline", [=] { return trend()->prev_trend_; }),
      Log("trendline_modified_offset",
          [=] { return trend()->prev_modified_trend_; }),
      Log("trendline_offset_threshold", [=] { return trend()->threshold_; }),
      Log("acknowledged_rate", [=] { return acknowledged_rate(); }),
      Log("est_capacity", [this] { return est_.link_capacity; }),
      Log("est_capacity_dev", [this] { return est_.link_capacity_std_dev; }),
      Log("est_capacity_min", [this] { return est_.link_capacity_min; }),
      Log("est_cross_traffic", [this] { return est_.cross_traffic_ratio; }),
      Log("est_cross_delay", [this] { return est_.cross_delay_rate; }),
      Log("est_spike_delay", [this] { return est_.spike_delay_rate; }),
      Log("est_pre_buffer", [this] { return est_.pre_link_buffer_delay; }),
      Log("est_post_buffer", [this] { return est_.post_link_buffer_delay; }),
      Log("est_propagation", [this] { return est_.propagation_delay; }),
      Log("data_window", [this] { return controller_->current_data_window_; }),
      Log("pushback_target",
          [this] { return controller_->last_pushback_target_rate_; }),
  });
  return loggers;
}
GoogCcStatePrinter::~GoogCcStatePrinter() = default;

void GoogCcStatePrinter::PrintHeaders(RtcEventLogOutput* log) {
  int ix = 0;
  for (const auto& logger : loggers_) {
    if (ix++)
      log->Write(" ");
    log->Write(logger->name());
  }
  log->Write("\n");
  log->Flush();
}

void GoogCcStatePrinter::PrintState(RtcEventLogOutput* log,
                                    GoogCcNetworkController* controller,
                                    Timestamp at_time) {
  controller_ = controller;
  auto state_update = controller_->GetNetworkState(at_time);
  target_ = state_update.target_rate.value();
  pacing_ = state_update.pacer_config.value();
  if (state_update.congestion_window)
    congestion_window_ = *state_update.congestion_window;
  if (controller_->network_estimator_) {
    est_ = controller_->network_estimator_->GetCurrentEstimate().value_or(
        NetworkStateEstimate());
  }

  int ix = 0;
  for (const auto& logger : loggers_) {
    if (ix++)
      log->Write(" ");
    logger->WriteValue(log);
  }

  log->Write("\n");
  log->Flush();
}

GoogCcDebugFactory::GoogCcDebugFactory()
    : GoogCcDebugFactory(GoogCcFactoryConfig()) {}

GoogCcDebugFactory::GoogCcDebugFactory(GoogCcFactoryConfig config)
    : GoogCcNetworkControllerFactory(std::move(config)) {}

std::unique_ptr<NetworkControllerInterface> GoogCcDebugFactory::Create(
    NetworkControllerConfig config) {
  RTC_CHECK(controller_ == nullptr);
  auto controller = GoogCcNetworkControllerFactory::Create(config);
  controller_ = static_cast<GoogCcNetworkController*>(controller.get());
  return controller;
}

void GoogCcDebugFactory::PrintState(const Timestamp at_time) {
  if (controller_ && log_writer_) {
    printer_.PrintState(log_writer_.get(), controller_, at_time);
  }
}

void GoogCcDebugFactory::AttachWriter(
    std::unique_ptr<RtcEventLogOutput> log_writer) {
  if (log_writer) {
    log_writer_ = std::move(log_writer);
    printer_.PrintHeaders(log_writer_.get());
  }
}

}  // namespace webrtc
