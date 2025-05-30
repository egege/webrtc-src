/*
 *  Copyright 2020 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "video/video_source_sink_controller.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "api/sequence_checker.h"
#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "api/video/video_source_interface.h"
#include "call/adaptation/video_source_restrictions.h"
#include "rtc_base/checks.h"
#include "rtc_base/numerics/safe_conversions.h"

namespace webrtc {

VideoSourceSinkController::VideoSourceSinkController(
    VideoSinkInterface<VideoFrame>* sink,
    VideoSourceInterface<VideoFrame>* source)
    : sink_(sink), source_(source) {
  RTC_DCHECK(sink_);
}

VideoSourceSinkController::~VideoSourceSinkController() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
}

void VideoSourceSinkController::SetSource(
    VideoSourceInterface<VideoFrame>* source) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);

  VideoSourceInterface<VideoFrame>* old_source = source_;
  source_ = source;

  if (old_source != source && old_source)
    old_source->RemoveSink(sink_);

  if (!source)
    return;

  source->AddOrUpdateSink(sink_, CurrentSettingsToSinkWants());
}

bool VideoSourceSinkController::HasSource() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return source_ != nullptr;
}

void VideoSourceSinkController::RequestRefreshFrame() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (source_)
    source_->RequestRefreshFrame();
}

void VideoSourceSinkController::PushSourceSinkSettings() {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  if (!source_)
    return;
  VideoSinkWants wants = CurrentSettingsToSinkWants();
  source_->AddOrUpdateSink(sink_, wants);
}

VideoSourceRestrictions VideoSourceSinkController::restrictions() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return restrictions_;
}

std::optional<size_t> VideoSourceSinkController::pixels_per_frame_upper_limit()
    const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return pixels_per_frame_upper_limit_;
}

std::optional<double> VideoSourceSinkController::frame_rate_upper_limit()
    const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return frame_rate_upper_limit_;
}

bool VideoSourceSinkController::rotation_applied() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return rotation_applied_;
}

int VideoSourceSinkController::resolution_alignment() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return resolution_alignment_;
}

const std::vector<VideoSinkWants::FrameSize>&
VideoSourceSinkController::resolutions() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return resolutions_;
}

bool VideoSourceSinkController::active() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return active_;
}

std::optional<VideoSinkWants::FrameSize>
VideoSourceSinkController::scale_resolution_down_to() const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  return scale_resolution_down_to_;
}

void VideoSourceSinkController::SetRestrictions(
    VideoSourceRestrictions restrictions) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  restrictions_ = std::move(restrictions);
}

void VideoSourceSinkController::SetPixelsPerFrameUpperLimit(
    std::optional<size_t> pixels_per_frame_upper_limit) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  pixels_per_frame_upper_limit_ = std::move(pixels_per_frame_upper_limit);
}

void VideoSourceSinkController::SetFrameRateUpperLimit(
    std::optional<double> frame_rate_upper_limit) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  frame_rate_upper_limit_ = std::move(frame_rate_upper_limit);
}

void VideoSourceSinkController::SetRotationApplied(bool rotation_applied) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  rotation_applied_ = rotation_applied;
}

void VideoSourceSinkController::SetResolutionAlignment(
    int resolution_alignment) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  resolution_alignment_ = resolution_alignment;
}

void VideoSourceSinkController::SetResolutions(
    std::vector<VideoSinkWants::FrameSize> resolutions) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  resolutions_ = std::move(resolutions);
}

void VideoSourceSinkController::SetActive(bool active) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  active_ = active;
}

void VideoSourceSinkController::SetScaleResolutionDownTo(
    std::optional<VideoSinkWants::FrameSize> scale_resolution_down_to) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  scale_resolution_down_to_ = std::move(scale_resolution_down_to);
}

// RTC_EXCLUSIVE_LOCKS_REQUIRED(sequence_checker_)
VideoSinkWants VideoSourceSinkController::CurrentSettingsToSinkWants() const {
  VideoSinkWants wants;
  wants.rotation_applied = rotation_applied_;
  // `wants.black_frames` is not used, it always has its default value false.
  wants.max_pixel_count =
      dchecked_cast<int>(restrictions_.max_pixels_per_frame().value_or(
          std::numeric_limits<int>::max()));
  wants.target_pixel_count =
      restrictions_.target_pixels_per_frame().has_value()
          ? std::optional<int>(dchecked_cast<int>(
                restrictions_.target_pixels_per_frame().value()))
          : std::nullopt;
  wants.max_framerate_fps =
      restrictions_.max_frame_rate().has_value()
          ? static_cast<int>(restrictions_.max_frame_rate().value())
          : std::numeric_limits<int>::max();
  wants.resolution_alignment = resolution_alignment_;
  wants.max_pixel_count =
      std::min(wants.max_pixel_count,
               dchecked_cast<int>(pixels_per_frame_upper_limit_.value_or(
                   std::numeric_limits<int>::max())));
  wants.max_framerate_fps =
      std::min(wants.max_framerate_fps,
               frame_rate_upper_limit_.has_value()
                   ? static_cast<int>(frame_rate_upper_limit_.value())
                   : std::numeric_limits<int>::max());
  wants.resolutions = resolutions_;
  wants.is_active = active_;
  wants.requested_resolution = scale_resolution_down_to_;
  return wants;
}

}  // namespace webrtc
