/*
 *  Copyright 2018 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#import <UIKit/UIKit.h>

#include "test/ios/coverage_util_ios.h"

int main(int argc, char* argv[]) {
  webrtc::test::ConfigureCoverageReportPath();

  @autoreleasepool {
    return UIApplicationMain(argc, argv, nil, nil);
  }
}
