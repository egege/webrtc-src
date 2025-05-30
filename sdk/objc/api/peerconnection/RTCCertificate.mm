/*
 *  Copyright 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import "RTCCertificate.h"

#import "base/RTCLogging.h"

#include "rtc_base/logging.h"
#include "rtc_base/rtc_certificate_generator.h"
#include "rtc_base/ssl_identity.h"

@implementation RTC_OBJC_TYPE (RTCCertificate)

@synthesize private_key = _private_key;
@synthesize certificate = _certificate;

- (id)copyWithZone:(NSZone *)zone {
  id copy = [[[self class] alloc]
      initWithPrivateKey:[self.private_key copyWithZone:zone]
             certificate:[self.certificate copyWithZone:zone]];
  return copy;
}

- (instancetype)initWithPrivateKey:(NSString *)private_key
                       certificate:(NSString *)certificate {
  self = [super init];
  if (self) {
    _private_key = [private_key copy];
    _certificate = [certificate copy];
  }
  return self;
}

+ (nullable RTC_OBJC_TYPE(RTCCertificate) *)generateCertificateWithParams:
    (NSDictionary *)params {
  webrtc::KeyType keyType = webrtc::KT_ECDSA;
  NSString *keyTypeString = [params valueForKey:@"name"];
  if (keyTypeString && [keyTypeString isEqualToString:@"RSASSA-PKCS1-v1_5"]) {
    keyType = webrtc::KT_RSA;
  }

  NSNumber *expires = [params valueForKey:@"expires"];
  webrtc::scoped_refptr<webrtc::RTCCertificate> cc_certificate = nullptr;
  if (expires != nil) {
    uint64_t expirationTimestamp = [expires unsignedLongLongValue];
    cc_certificate = webrtc::RTCCertificateGenerator::GenerateCertificate(
        webrtc::KeyParams(keyType), expirationTimestamp);
  } else {
    cc_certificate = webrtc::RTCCertificateGenerator::GenerateCertificate(
        webrtc::KeyParams(keyType), std::nullopt);
  }
  if (!cc_certificate) {
    RTCLogError(@"Failed to generate certificate.");
    return nullptr;
  }
  // grab PEMs and create an NS RTCCerticicate
  webrtc::RTCCertificatePEM pem = cc_certificate->ToPEM();
  std::string pem_private_key = pem.private_key();
  std::string pem_certificate = pem.certificate();
  RTC_LOG(LS_INFO) << "CERT PEM ";
  RTC_LOG(LS_INFO) << pem_certificate;

  RTC_OBJC_TYPE(RTCCertificate) *cert = [[RTC_OBJC_TYPE(RTCCertificate) alloc]
      initWithPrivateKey:@(pem_private_key.c_str())
             certificate:@(pem_certificate.c_str())];
  return cert;
}

@end
