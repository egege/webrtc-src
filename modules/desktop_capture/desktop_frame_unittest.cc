/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/desktop_capture/desktop_frame.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>

#include "api/array_view.h"
#include "modules/desktop_capture/desktop_geometry.h"
#include "test/gtest.h"

namespace webrtc {

namespace {

std::unique_ptr<DesktopFrame> CreateTestFrame(DesktopRect rect,
                                              int pixels_value) {
  DesktopSize size = rect.size();
  auto frame = std::make_unique<BasicDesktopFrame>(size);
  frame->set_top_left(rect.top_left());
  memset(frame->data(), pixels_value, frame->stride() * size.height());
  return frame;
}

struct TestData {
  const char* description;
  DesktopRect dest_frame_rect;
  DesktopRect src_frame_rect;
  double horizontal_scale;
  double vertical_scale;
  DesktopRect expected_overlap_rect;
};

void RunTest(const TestData& test) {
  // Copy a source frame with all bits set into a dest frame with none set.
  auto dest_frame = CreateTestFrame(test.dest_frame_rect, 0);
  auto src_frame = CreateTestFrame(test.src_frame_rect, 0xff);

  dest_frame->CopyIntersectingPixelsFrom(*src_frame, test.horizontal_scale,
                                         test.vertical_scale);

  // Translate the expected overlap rect to be relative to the dest frame/rect.
  DesktopVector dest_frame_origin = test.dest_frame_rect.top_left();
  DesktopRect relative_expected_overlap_rect = test.expected_overlap_rect;
  relative_expected_overlap_rect.Translate(-dest_frame_origin.x(),
                                           -dest_frame_origin.y());

  // Confirm bits are now set in the dest frame if & only if they fall in the
  // expected range.
  for (int y = 0; y < dest_frame->size().height(); ++y) {
    SCOPED_TRACE(y);

    for (int x = 0; x < dest_frame->size().width(); ++x) {
      SCOPED_TRACE(x);

      DesktopVector point(x, y);
      uint8_t* data = dest_frame->GetFrameDataAtPos(point);
      uint32_t pixel_value = *reinterpret_cast<uint32_t*>(data);
      bool was_copied = pixel_value == 0xffffffff;
      ASSERT_TRUE(was_copied || pixel_value == 0);

      bool expected_to_be_copied =
          relative_expected_overlap_rect.Contains(point);

      ASSERT_EQ(was_copied, expected_to_be_copied);
    }
  }
}

void RunTests(ArrayView<const TestData> tests) {
  for (const TestData& test : tests) {
    SCOPED_TRACE(test.description);

    RunTest(test);
  }
}

}  // namespace

TEST(DesktopFrameTest, NewFrameIsBlack) {
  auto frame = std::make_unique<BasicDesktopFrame>(DesktopSize(10, 10));
  EXPECT_TRUE(frame->FrameDataIsBlack());
}

TEST(DesktopFrameTest, EmptyFrameIsNotBlack) {
  auto frame = std::make_unique<BasicDesktopFrame>(DesktopSize());
  EXPECT_FALSE(frame->FrameDataIsBlack());
}

TEST(DesktopFrameTest, FrameHasDefaultDeviceScaleFactor) {
  auto frame = std::make_unique<BasicDesktopFrame>(DesktopSize());
  EXPECT_EQ(frame->device_scale_factor(), std::nullopt);
}

TEST(DesktopFrameTest, FrameSetsDeviceScaleFactorCorrectly) {
  auto frame = std::make_unique<BasicDesktopFrame>(DesktopSize());
  EXPECT_EQ(frame->device_scale_factor(), std::nullopt);
  float device_scale_factor = 1.5f;
  frame->set_device_scale_factor(device_scale_factor);
  EXPECT_EQ(frame->device_scale_factor(), device_scale_factor);
}

TEST(DesktopFrameTest, FrameDataSwitchesBetweenNonBlackAndBlack) {
  auto frame = CreateTestFrame(DesktopRect::MakeXYWH(0, 0, 10, 10), 0xff);
  EXPECT_FALSE(frame->FrameDataIsBlack());
  frame->SetFrameDataToBlack();
  EXPECT_TRUE(frame->FrameDataIsBlack());
}

TEST(DesktopFrameTest, CopyIntersectingPixelsMatchingRects) {
  // clang-format off
  const TestData tests[] = {
    {"0 origin",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     1.0, 1.0,
     DesktopRect::MakeXYWH(0, 0, 2, 2)},

    {"Negative origin",
     DesktopRect::MakeXYWH(-1, -1, 2, 2),
     DesktopRect::MakeXYWH(-1, -1, 2, 2),
     1.0, 1.0,
     DesktopRect::MakeXYWH(-1, -1, 2, 2)}
  };
  // clang-format on

  RunTests(tests);
}

TEST(DesktopFrameTest, CopyIntersectingPixelsMatchingRectsScaled) {
  // The scale factors shouldn't affect matching rects (they're only applied
  // to any difference between the origins)
  // clang-format off
  const TestData tests[] = {
    {"0 origin 2x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     2.0, 2.0,
     DesktopRect::MakeXYWH(0, 0, 2, 2)},

    {"0 origin 0.5x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     0.5, 0.5,
     DesktopRect::MakeXYWH(0, 0, 2, 2)},

    {"Negative origin 2x",
     DesktopRect::MakeXYWH(-1, -1, 2, 2),
     DesktopRect::MakeXYWH(-1, -1, 2, 2),
     2.0, 2.0,
     DesktopRect::MakeXYWH(-1, -1, 2, 2)},

    {"Negative origin 0.5x",
     DesktopRect::MakeXYWH(-1, -1, 2, 2),
     DesktopRect::MakeXYWH(-1, -1, 2, 2),
     0.5, 0.5,
     DesktopRect::MakeXYWH(-1, -1, 2, 2)}
  };
  // clang-format on

  RunTests(tests);
}

TEST(DesktopFrameTest, CopyIntersectingPixelsFullyContainedRects) {
  // clang-format off
  const TestData tests[] = {
    {"0 origin top left",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, 0, 1, 1),
     1.0, 1.0,
     DesktopRect::MakeXYWH(0, 0, 1, 1)},

    {"0 origin bottom right",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(1, 1, 1, 1),
     1.0, 1.0,
     DesktopRect::MakeXYWH(1, 1, 1, 1)},

    {"Negative origin bottom left",
     DesktopRect::MakeXYWH(-1, -1, 2, 2),
     DesktopRect::MakeXYWH(-1, 0, 1, 1),
     1.0, 1.0,
     DesktopRect::MakeXYWH(-1, 0, 1, 1)}
  };
  // clang-format on

  RunTests(tests);
}

TEST(DesktopFrameTest, CopyIntersectingPixelsFullyContainedRectsScaled) {
  // clang-format off
  const TestData tests[] = {
    {"0 origin top left 2x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, 0, 1, 1),
     2.0, 2.0,
     DesktopRect::MakeXYWH(0, 0, 1, 1)},

    {"0 origin top left 0.5x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, 0, 1, 1),
     0.5, 0.5,
     DesktopRect::MakeXYWH(0, 0, 1, 1)},

    {"0 origin bottom left 2x",
     DesktopRect::MakeXYWH(0, 0, 4, 4),
     DesktopRect::MakeXYWH(1, 1, 2, 2),
     2.0, 2.0,
     DesktopRect::MakeXYWH(2, 2, 2, 2)},

    {"0 origin bottom middle 2x/1x",
     DesktopRect::MakeXYWH(0, 0, 4, 3),
     DesktopRect::MakeXYWH(1, 1, 2, 2),
     2.0, 1.0,
     DesktopRect::MakeXYWH(2, 1, 2, 2)},

    {"0 origin middle 0.5x",
     DesktopRect::MakeXYWH(0, 0, 3, 3),
     DesktopRect::MakeXYWH(2, 2, 1, 1),
     0.5, 0.5,
     DesktopRect::MakeXYWH(1, 1, 1, 1)},

    {"Negative origin bottom left 2x",
     DesktopRect::MakeXYWH(-1, -1, 3, 3),
     DesktopRect::MakeXYWH(-1, 0, 1, 1),
     2.0, 2.0,
     DesktopRect::MakeXYWH(-1, 1, 1, 1)},

    {"Negative origin near middle 0.5x",
     DesktopRect::MakeXYWH(-2, -2, 2, 2),
     DesktopRect::MakeXYWH(0, 0, 1, 1),
     0.5, 0.5,
     DesktopRect::MakeXYWH(-1, -1, 1, 1)}
  };
  // clang-format on

  RunTests(tests);
}

TEST(DesktopFrameTest, CopyIntersectingPixelsPartiallyContainedRects) {
  // clang-format off
  const TestData tests[] = {
    {"Top left",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(-1, -1, 2, 2),
     1.0, 1.0,
     DesktopRect::MakeXYWH(0, 0, 1, 1)},

    {"Top right",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(1, -1, 2, 2),
     1.0, 1.0,
     DesktopRect::MakeXYWH(1, 0, 1, 1)},

    {"Bottom right",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(1, 1, 2, 2),
     1.0, 1.0,
     DesktopRect::MakeXYWH(1, 1, 1, 1)},

    {"Bottom left",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(-1, 1, 2, 2),
     1.0, 1.0,
     DesktopRect::MakeXYWH(0, 1, 1, 1)}
  };
  // clang-format on

  RunTests(tests);
}

TEST(DesktopFrameTest, CopyIntersectingPixelsPartiallyContainedRectsScaled) {
  // clang-format off
  const TestData tests[] = {
    {"Top left 2x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(-1, -1, 3, 3),
     2.0, 2.0,
     DesktopRect::MakeXYWH(0, 0, 1, 1)},

    {"Top right 0.5x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(2, -2, 2, 2),
     0.5, 0.5,
     DesktopRect::MakeXYWH(1, 0, 1, 1)},

    {"Bottom right 2x",
     DesktopRect::MakeXYWH(0, 0, 3, 3),
     DesktopRect::MakeXYWH(-1, 1, 3, 3),
     2.0, 2.0,
     DesktopRect::MakeXYWH(0, 2, 1, 1)},

    {"Bottom left 0.5x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(-2, 2, 2, 2),
     0.5, 0.5,
     DesktopRect::MakeXYWH(0, 1, 1, 1)}
  };
  // clang-format on

  RunTests(tests);
}

TEST(DesktopFrameTest, CopyIntersectingPixelsUncontainedRects) {
  // clang-format off
  const TestData tests[] = {
    {"Left",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(-1, 0, 1, 2),
     1.0, 1.0,
     DesktopRect::MakeXYWH(0, 0, 0, 0)},

    {"Top",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, -1, 2, 1),
     1.0, 1.0,
     DesktopRect::MakeXYWH(0, 0, 0, 0)},

    {"Right",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(2, 0, 1, 2),
     1.0, 1.0,
     DesktopRect::MakeXYWH(0, 0, 0, 0)},


    {"Bottom",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, 2, 2, 1),
     1.0, 1.0,
     DesktopRect::MakeXYWH(0, 0, 0, 0)}
  };
  // clang-format on

  RunTests(tests);
}

TEST(DesktopFrameTest, CopyIntersectingPixelsUncontainedRectsScaled) {
  // clang-format off
  const TestData tests[] = {
    {"Left 2x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(-1, 0, 2, 2),
     2.0, 2.0,
     DesktopRect::MakeXYWH(0, 0, 0, 0)},

    {"Top 0.5x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, -2, 2, 1),
     0.5, 0.5,
     DesktopRect::MakeXYWH(0, 0, 0, 0)},

    {"Right 2x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(1, 0, 1, 2),
     2.0, 2.0,
     DesktopRect::MakeXYWH(0, 0, 0, 0)},


    {"Bottom 0.5x",
     DesktopRect::MakeXYWH(0, 0, 2, 2),
     DesktopRect::MakeXYWH(0, 4, 2, 1),
     0.5, 0.5,
     DesktopRect::MakeXYWH(0, 0, 0, 0)}
  };
  // clang-format on

  RunTests(tests);
}

}  // namespace webrtc
