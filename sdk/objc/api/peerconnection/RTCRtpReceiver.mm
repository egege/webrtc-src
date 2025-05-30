/*
 *  Copyright 2016 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "RTCRtpReceiver+Private.h"

#import "RTCMediaStreamTrack+Private.h"
#import "RTCRtpParameters+Private.h"
#import "RTCRtpReceiver+Native.h"
#import "RTCRtpSource+Private.h"
#import "base/RTCLogging.h"
#import "helpers/NSString+StdString.h"

#include "api/media_stream_interface.h"

namespace webrtc {

RtpReceiverDelegateAdapter::RtpReceiverDelegateAdapter(
    RTC_OBJC_TYPE(RTCRtpReceiver) * receiver) {
  RTC_CHECK(receiver);
  receiver_ = receiver;
}

void RtpReceiverDelegateAdapter::OnFirstPacketReceived(
    webrtc::MediaType media_type) {
  RTCRtpMediaType packet_media_type =
      [RTC_OBJC_TYPE(RTCRtpReceiver) mediaTypeForNativeMediaType:media_type];
  RTC_OBJC_TYPE(RTCRtpReceiver) *receiver = receiver_;
  [receiver.delegate rtpReceiver:receiver
      didReceiveFirstPacketForMediaType:packet_media_type];
}

}  // namespace webrtc

@implementation RTC_OBJC_TYPE (RTCRtpReceiver) {
  RTC_OBJC_TYPE(RTCPeerConnectionFactory) * _factory;
  webrtc::scoped_refptr<webrtc::RtpReceiverInterface> _nativeRtpReceiver;
  std::unique_ptr<webrtc::RtpReceiverDelegateAdapter> _observer;
}

@synthesize delegate = _delegate;

- (NSString *)receiverId {
  return [NSString stringForStdString:_nativeRtpReceiver->id()];
}

- (RTC_OBJC_TYPE(RTCRtpParameters) *)parameters {
  return [[RTC_OBJC_TYPE(RTCRtpParameters) alloc]
      initWithNativeParameters:_nativeRtpReceiver->GetParameters()];
}

- (nullable RTC_OBJC_TYPE(RTCMediaStreamTrack) *)track {
  webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> nativeTrack(
      _nativeRtpReceiver->track());
  if (nativeTrack) {
    return
        [RTC_OBJC_TYPE(RTCMediaStreamTrack) mediaTrackForNativeTrack:nativeTrack
                                                             factory:_factory];
  }
  return nil;
}

- (NSString *)description {
  return [NSString
      stringWithFormat:@"RTC_OBJC_TYPE(RTCRtpReceiver) {\n  receiverId: %@\n}",
                       self.receiverId];
}

- (NSArray<RTC_OBJC_TYPE(RTCRtpSource) *> *)sources {
  std::vector<webrtc::RtpSource> nativeSources =
      _nativeRtpReceiver->GetSources();
  NSMutableArray<RTC_OBJC_TYPE(RTCRtpSource) *> *result =
      [[NSMutableArray alloc] initWithCapacity:nativeSources.size()];
  for (auto nativeSource : nativeSources) {
    [result addObject:[[RTC_OBJC_TYPE(RTCRtpSource) alloc]
                          initWithNativeRtpSource:nativeSource]];
  }
  return result;
}

- (void)dealloc {
  if (_nativeRtpReceiver) {
    _nativeRtpReceiver->SetObserver(nullptr);
  }
}

- (BOOL)isEqual:(id)object {
  if (self == object) {
    return YES;
  }
  if (object == nil) {
    return NO;
  }
  if (![object isMemberOfClass:[self class]]) {
    return NO;
  }
  RTC_OBJC_TYPE(RTCRtpReceiver) *receiver =
      (RTC_OBJC_TYPE(RTCRtpReceiver) *)object;
  return _nativeRtpReceiver == receiver.nativeRtpReceiver;
}

- (NSUInteger)hash {
  return (NSUInteger)_nativeRtpReceiver.get();
}

#pragma mark - Native

- (void)setFrameDecryptor:
    (webrtc::scoped_refptr<webrtc::FrameDecryptorInterface>)frameDecryptor {
  _nativeRtpReceiver->SetFrameDecryptor(frameDecryptor);
}

#pragma mark - Private

- (webrtc::scoped_refptr<webrtc::RtpReceiverInterface>)nativeRtpReceiver {
  return _nativeRtpReceiver;
}

- (instancetype)
      initWithFactory:(RTC_OBJC_TYPE(RTCPeerConnectionFactory) *)factory
    nativeRtpReceiver:
        (webrtc::scoped_refptr<webrtc::RtpReceiverInterface>)nativeRtpReceiver {
  self = [super init];
  if (self) {
    _factory = factory;
    _nativeRtpReceiver = nativeRtpReceiver;
    RTCLogInfo(@"RTC_OBJC_TYPE(RTCRtpReceiver)(%p): created receiver: %@",
               self,
               self.description);
    _observer.reset(new webrtc::RtpReceiverDelegateAdapter(self));
    _nativeRtpReceiver->SetObserver(_observer.get());
  }
  return self;
}

+ (RTCRtpMediaType)mediaTypeForNativeMediaType:
    (webrtc::MediaType)nativeMediaType {
  switch (nativeMediaType) {
    case webrtc::MediaType::AUDIO:
      return RTCRtpMediaTypeAudio;
    case webrtc::MediaType::VIDEO:
      return RTCRtpMediaTypeVideo;
    case webrtc::MediaType::DATA:
      return RTCRtpMediaTypeData;
    case webrtc::MediaType::UNSUPPORTED:
      return RTCRtpMediaTypeUnsupported;
    case webrtc::MediaType::ANY:
      return RTCRtpMediaTypeAny;
  }
}

+ (webrtc::MediaType)nativeMediaTypeForMediaType:(RTCRtpMediaType)mediaType {
  switch (mediaType) {
    case RTCRtpMediaTypeAudio:
      return webrtc::MediaType::AUDIO;
    case RTCRtpMediaTypeVideo:
      return webrtc::MediaType::VIDEO;
    case RTCRtpMediaTypeData:
      return webrtc::MediaType::DATA;
    case RTCRtpMediaTypeUnsupported:
      return webrtc::MediaType::UNSUPPORTED;
    case RTCRtpMediaTypeAny:
      return webrtc::MediaType::ANY;
  }
}

+ (NSString *)stringForMediaType:(RTCRtpMediaType)mediaType {
  switch (mediaType) {
    case RTCRtpMediaTypeAudio:
      return @"AUDIO";
    case RTCRtpMediaTypeVideo:
      return @"VIDEO";
    case RTCRtpMediaTypeData:
      return @"DATA";
    case RTCRtpMediaTypeUnsupported:
      return @"UNSUPPORTED";
    case RTCRtpMediaTypeAny:
      return @"ANY";
  }
}

@end
