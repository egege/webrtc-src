/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 *
 */

#import "RTCH264ProfileLevelId.h"

#import "helpers/NSString+StdString.h"
#if defined(WEBRTC_IOS)
#import "UIDevice+H264Profile.h"
#endif

#include "api/video_codecs/h264_profile_level_id.h"
#include "media/base/media_constants.h"

namespace {

NSString *MaxSupportedProfileLevelConstrainedHigh();
NSString *MaxSupportedProfileLevelConstrainedBaseline();

}  // namespace

NSString *const kRTCVideoCodecH264Name = @(webrtc::kH264CodecName);
NSString *const kRTCLevel31ConstrainedHigh = @"640c1f";
NSString *const kRTCLevel31ConstrainedBaseline = @"42e01f";
NSString *const kRTCMaxSupportedH264ProfileLevelConstrainedHigh =
    MaxSupportedProfileLevelConstrainedHigh();
NSString *const kRTCMaxSupportedH264ProfileLevelConstrainedBaseline =
    MaxSupportedProfileLevelConstrainedBaseline();

namespace {

#if defined(WEBRTC_IOS)

NSString *MaxSupportedLevelForProfile(webrtc::H264Profile profile) {
  const std::optional<webrtc::H264ProfileLevelId> profileLevelId =
      [UIDevice maxSupportedH264Profile];
  if (profileLevelId && profileLevelId->profile >= profile) {
    const std::optional<std::string> profileString = H264ProfileLevelIdToString(
        webrtc::H264ProfileLevelId(profile, profileLevelId->level));
    if (profileString) {
      return [NSString stringForStdString:*profileString];
    }
  }
  return nil;
}
#endif

NSString *MaxSupportedProfileLevelConstrainedBaseline() {
#if defined(WEBRTC_IOS)
  NSString *profile = MaxSupportedLevelForProfile(
      webrtc::H264Profile::kProfileConstrainedBaseline);
  if (profile != nil) {
    return profile;
  }
#endif
  return kRTCLevel31ConstrainedBaseline;
}

NSString *MaxSupportedProfileLevelConstrainedHigh() {
#if defined(WEBRTC_IOS)
  NSString *profile =
      MaxSupportedLevelForProfile(webrtc::H264Profile::kProfileConstrainedHigh);
  if (profile != nil) {
    return profile;
  }
#endif
  return kRTCLevel31ConstrainedHigh;
}

}  // namespace

@interface RTC_OBJC_TYPE (RTCH264ProfileLevelId)
()

    @property(nonatomic, assign) RTCH264Profile profile;
@property(nonatomic, assign) RTCH264Level level;
@property(nonatomic, strong) NSString *hexString;

@end

@implementation RTC_OBJC_TYPE (RTCH264ProfileLevelId)

@synthesize profile = _profile;
@synthesize level = _level;
@synthesize hexString = _hexString;

- (instancetype)initWithHexString:(NSString *)hexString {
  self = [super init];
  if (self) {
    self.hexString = hexString;

    std::optional<webrtc::H264ProfileLevelId> profile_level_id =
        webrtc::ParseH264ProfileLevelId(
            [hexString cStringUsingEncoding:NSUTF8StringEncoding]);
    if (profile_level_id.has_value()) {
      self.profile = static_cast<RTCH264Profile>(profile_level_id->profile);
      self.level = static_cast<RTCH264Level>(profile_level_id->level);
    }
  }
  return self;
}

- (instancetype)initWithProfile:(RTCH264Profile)profile
                          level:(RTCH264Level)level {
  self = [super init];
  if (self) {
    self.profile = profile;
    self.level = level;

    std::optional<std::string> hex_string = webrtc::H264ProfileLevelIdToString(
        webrtc::H264ProfileLevelId(static_cast<webrtc::H264Profile>(profile),
                                   static_cast<webrtc::H264Level>(level)));
    self.hexString = [NSString stringWithCString:hex_string.value_or("").c_str()
                                        encoding:NSUTF8StringEncoding];
  }
  return self;
}

@end
