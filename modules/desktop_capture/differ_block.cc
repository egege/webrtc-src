/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/differ_block.h"

#include <cstdint>
#include <cstring>

#include "rtc_base/cpu_info.h"

// Defines WEBRTC_ARCH_X86_FAMILY, used below.
#include "rtc_base/system/arch.h"
#if defined(WEBRTC_ARCH_X86_FAMILY)
#include "modules/desktop_capture/differ_vector_sse2.h"
#endif

namespace webrtc {

namespace {

bool VectorDifference_C(const uint8_t* image1, const uint8_t* image2) {
  return memcmp(image1, image2, kBlockSize * kBytesPerPixel) != 0;
}

}  // namespace

bool VectorDifference(const uint8_t* image1, const uint8_t* image2) {
  static bool (*diff_proc)(const uint8_t*, const uint8_t*) = nullptr;

  if (!diff_proc) {
#if defined(WEBRTC_ARCH_X86_FAMILY)
    bool have_sse2 = cpu_info::Supports(cpu_info::ISA::kSSE2);
    // For x86 processors, check if SSE2 is supported.
    if (have_sse2 && kBlockSize == 32) {
      diff_proc = &VectorDifference_SSE2_W32;
    } else if (have_sse2 && kBlockSize == 16) {
      diff_proc = &VectorDifference_SSE2_W16;
    } else {
      diff_proc = &VectorDifference_C;
    }
#else
    // For other processors, always use C version.
    // TODO(hclam): Implement a NEON version.
    diff_proc = &VectorDifference_C;
#endif
  }

  return diff_proc(image1, image2);
}

bool BlockDifference(const uint8_t* image1,
                     const uint8_t* image2,
                     int height,
                     int stride) {
  for (int i = 0; i < height; i++) {
    if (VectorDifference(image1, image2)) {
      return true;
    }
    image1 += stride;
    image2 += stride;
  }
  return false;
}

bool BlockDifference(const uint8_t* image1, const uint8_t* image2, int stride) {
  return BlockDifference(image1, image2, kBlockSize, stride);
}

}  // namespace webrtc
