/*
 *  Copyright (c) 2022 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/linux/wayland/mouse_cursor_monitor_pipewire.h"

#include <memory>
#include <optional>

#include "api/sequence_checker.h"
#include "modules/desktop_capture/desktop_capture_options.h"
#include "modules/desktop_capture/desktop_capturer.h"
#include "modules/desktop_capture/desktop_geometry.h"
#include "modules/desktop_capture/mouse_cursor.h"
#include "modules/desktop_capture/mouse_cursor_monitor.h"
#include "rtc_base/checks.h"

namespace webrtc {

MouseCursorMonitorPipeWire::MouseCursorMonitorPipeWire(
    const DesktopCaptureOptions& options)
    : options_(options) {
  sequence_checker_.Detach();
}

MouseCursorMonitorPipeWire::~MouseCursorMonitorPipeWire() {}

void MouseCursorMonitorPipeWire::Init(Callback* callback, Mode mode) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DCHECK(!callback_);
  RTC_DCHECK(callback);

  callback_ = callback;
  mode_ = mode;
}

void MouseCursorMonitorPipeWire::Capture() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  RTC_DCHECK(callback_);

  std::optional<DesktopVector> mouse_cursor_position =
      options_.screencast_stream()->CaptureCursorPosition();
  // Invalid cursor or position
  if (!mouse_cursor_position) {
    callback_->OnMouseCursor(nullptr);
    return;
  }

  std::unique_ptr<MouseCursor> mouse_cursor =
      options_.screencast_stream()->CaptureCursor();

  if (mouse_cursor && mouse_cursor->image()->data()) {
    callback_->OnMouseCursor(mouse_cursor.release());
  }

  if (mode_ == SHAPE_AND_POSITION) {
    callback_->OnMouseCursorPosition(mouse_cursor_position.value());
  }
}

}  // namespace webrtc
