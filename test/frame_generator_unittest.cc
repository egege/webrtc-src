/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <stdio.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "api/scoped_refptr.h"
#include "api/test/create_frame_generator.h"
#include "api/test/frame_generator_interface.h"
#include "api/video/video_frame_buffer.h"
#include "rtc_base/checks.h"
#include "test/gtest.h"
#include "test/testsupport/file_utils.h"

namespace webrtc {
namespace test {

constexpr int kFrameWidth = 4;
constexpr int kFrameHeight = 4;
constexpr int y_size = kFrameWidth * kFrameHeight;
constexpr int uv_size = ((kFrameHeight + 1) / 2) * ((kFrameWidth + 1) / 2);

class FrameGeneratorTest : public ::testing::Test {
 public:
  void SetUp() override {
    two_frame_yuv_filename_ =
        test::TempFilename(test::OutputPath(), "2_frame_yuv_file");
    one_frame_yuv_filename_ =
        test::TempFilename(test::OutputPath(), "1_frame_yuv_file");
    two_frame_nv12_filename_ =
        test::TempFilename(test::OutputPath(), "2_frame_nv12_file");
    one_frame_nv12_filename_ =
        test::TempFilename(test::OutputPath(), "1_frame_nv12_file");

    FILE* file = fopen(two_frame_yuv_filename_.c_str(), "wb");
    WriteYuvFile(file, 0, 0, 0);
    WriteYuvFile(file, 127, 128, 129);
    fclose(file);
    file = fopen(one_frame_yuv_filename_.c_str(), "wb");
    WriteYuvFile(file, 255, 255, 255);
    fclose(file);
    file = fopen(two_frame_nv12_filename_.c_str(), "wb");
    WriteNV12File(file, 0, 0, 0);
    WriteNV12File(file, 127, 128, 129);
    fclose(file);
    file = fopen(one_frame_nv12_filename_.c_str(), "wb");
    WriteNV12File(file, 255, 255, 255);
    fclose(file);
  }

  void TearDown() override {
    remove(one_frame_yuv_filename_.c_str());
    remove(two_frame_yuv_filename_.c_str());
    remove(one_frame_nv12_filename_.c_str());
    remove(two_frame_nv12_filename_.c_str());
  }

 protected:
  void WriteYuvFile(FILE* file, uint8_t y, uint8_t u, uint8_t v) {
    RTC_DCHECK(file);
    std::unique_ptr<uint8_t[]> plane_buffer(new uint8_t[y_size]);
    memset(plane_buffer.get(), y, y_size);
    fwrite(plane_buffer.get(), 1, y_size, file);
    memset(plane_buffer.get(), u, uv_size);
    fwrite(plane_buffer.get(), 1, uv_size, file);
    memset(plane_buffer.get(), v, uv_size);
    fwrite(plane_buffer.get(), 1, uv_size, file);
  }

  void WriteNV12File(FILE* file, uint8_t y, uint8_t u, uint8_t v) {
    RTC_DCHECK(file);
    uint8_t plane_buffer[y_size];

    memset(&plane_buffer, y, y_size);
    fwrite(&plane_buffer, 1, y_size, file);
    for (size_t i = 0; i < uv_size; ++i) {
      plane_buffer[2 * i] = u;
      plane_buffer[2 * i + 1] = v;
    }
    fwrite(&plane_buffer, 1, 2 * uv_size, file);
  }

  void CheckFrameAndMutate(const FrameGeneratorInterface::VideoFrameData& frame,
                           uint8_t y,
                           uint8_t u,
                           uint8_t v) {
    // Check that frame is valid, has the correct color and timestamp are clean.
    scoped_refptr<I420BufferInterface> i420_buffer = frame.buffer->ToI420();
    const uint8_t* buffer;
    buffer = i420_buffer->DataY();
    for (int i = 0; i < y_size; ++i)
      ASSERT_EQ(y, buffer[i]);
    buffer = i420_buffer->DataU();
    for (int i = 0; i < uv_size; ++i)
      ASSERT_EQ(u, buffer[i]);
    buffer = i420_buffer->DataV();
    for (int i = 0; i < uv_size; ++i)
      ASSERT_EQ(v, buffer[i]);
  }

  uint64_t Hash(const FrameGeneratorInterface::VideoFrameData& frame) {
    // Generate a 64-bit hash from the frame's buffer.
    uint64_t hash = 19;
    scoped_refptr<I420BufferInterface> i420_buffer = frame.buffer->ToI420();
    const uint8_t* buffer = i420_buffer->DataY();
    for (int i = 0; i < y_size; ++i) {
      hash = (37 * hash) + buffer[i];
    }
    buffer = i420_buffer->DataU();
    for (int i = 0; i < uv_size; ++i) {
      hash = (37 * hash) + buffer[i];
    }
    buffer = i420_buffer->DataV();
    for (int i = 0; i < uv_size; ++i) {
      hash = (37 * hash) + buffer[i];
    }
    return hash;
  }

  std::string two_frame_yuv_filename_;
  std::string one_frame_yuv_filename_;
  std::string two_frame_nv12_filename_;
  std::string one_frame_nv12_filename_;
};

TEST_F(FrameGeneratorTest, SingleFrameYuvFile) {
  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromYuvFileFrameGenerator(
          std::vector<std::string>(1, one_frame_yuv_filename_), kFrameWidth,
          kFrameHeight, 1));
  CheckFrameAndMutate(generator->NextFrame(), 255, 255, 255);
  CheckFrameAndMutate(generator->NextFrame(), 255, 255, 255);
}

TEST_F(FrameGeneratorTest, TwoFrameYuvFile) {
  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromYuvFileFrameGenerator(
          std::vector<std::string>(1, two_frame_yuv_filename_), kFrameWidth,
          kFrameHeight, 1));
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
  CheckFrameAndMutate(generator->NextFrame(), 127, 128, 129);
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
}

TEST_F(FrameGeneratorTest, MultipleFrameYuvFiles) {
  std::vector<std::string> files;
  files.push_back(two_frame_yuv_filename_);
  files.push_back(one_frame_yuv_filename_);

  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromYuvFileFrameGenerator(files, kFrameWidth, kFrameHeight, 1));
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
  CheckFrameAndMutate(generator->NextFrame(), 127, 128, 129);
  CheckFrameAndMutate(generator->NextFrame(), 255, 255, 255);
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
}

TEST_F(FrameGeneratorTest, TwoFrameYuvFileWithRepeat) {
  const int kRepeatCount = 3;
  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromYuvFileFrameGenerator(
          std::vector<std::string>(1, two_frame_yuv_filename_), kFrameWidth,
          kFrameHeight, kRepeatCount));
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 127, 128, 129);
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
}

TEST_F(FrameGeneratorTest, MultipleFrameYuvFilesWithRepeat) {
  const int kRepeatCount = 3;
  std::vector<std::string> files;
  files.push_back(two_frame_yuv_filename_);
  files.push_back(one_frame_yuv_filename_);
  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromYuvFileFrameGenerator(files, kFrameWidth, kFrameHeight,
                                      kRepeatCount));
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 127, 128, 129);
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 255, 255, 255);
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
}

TEST_F(FrameGeneratorTest, SingleFrameNV12File) {
  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromNV12FileFrameGenerator(
          std::vector<std::string>(1, one_frame_nv12_filename_), kFrameWidth,
          kFrameHeight, 1));
  CheckFrameAndMutate(generator->NextFrame(), 255, 255, 255);
  CheckFrameAndMutate(generator->NextFrame(), 255, 255, 255);
}

TEST_F(FrameGeneratorTest, TwoFrameNV12File) {
  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromNV12FileFrameGenerator(
          std::vector<std::string>(1, two_frame_nv12_filename_), kFrameWidth,
          kFrameHeight, 1));
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
  CheckFrameAndMutate(generator->NextFrame(), 127, 128, 129);
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
}

TEST_F(FrameGeneratorTest, MultipleFrameNV12Files) {
  std::vector<std::string> files;
  files.push_back(two_frame_nv12_filename_);
  files.push_back(one_frame_nv12_filename_);

  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromNV12FileFrameGenerator(files, kFrameWidth, kFrameHeight, 1));
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
  CheckFrameAndMutate(generator->NextFrame(), 127, 128, 129);
  CheckFrameAndMutate(generator->NextFrame(), 255, 255, 255);
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
}

TEST_F(FrameGeneratorTest, TwoFrameNV12FileWithRepeat) {
  const int kRepeatCount = 3;
  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromNV12FileFrameGenerator(
          std::vector<std::string>(1, two_frame_nv12_filename_), kFrameWidth,
          kFrameHeight, kRepeatCount));
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 127, 128, 129);
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
}

TEST_F(FrameGeneratorTest, MultipleFrameNV12FilesWithRepeat) {
  const int kRepeatCount = 3;
  std::vector<std::string> files;
  files.push_back(two_frame_nv12_filename_);
  files.push_back(one_frame_nv12_filename_);
  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateFromNV12FileFrameGenerator(files, kFrameWidth, kFrameHeight,
                                       kRepeatCount));
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 127, 128, 129);
  for (int i = 0; i < kRepeatCount; ++i)
    CheckFrameAndMutate(generator->NextFrame(), 255, 255, 255);
  CheckFrameAndMutate(generator->NextFrame(), 0, 0, 0);
}

TEST_F(FrameGeneratorTest, SlideGenerator) {
  const int kGenCount = 9;
  const int kRepeatCount = 3;
  std::unique_ptr<FrameGeneratorInterface> generator(
      CreateSlideFrameGenerator(kFrameWidth, kFrameHeight, kRepeatCount));
  uint64_t hashes[kGenCount];
  for (int i = 0; i < kGenCount; ++i) {
    hashes[i] = Hash(generator->NextFrame());
  }
  // Check that the buffer changes only every `kRepeatCount` frames.
  for (int i = 1; i < kGenCount; ++i) {
    if (i % kRepeatCount == 0) {
      EXPECT_NE(hashes[i - 1], hashes[i]);
    } else {
      EXPECT_EQ(hashes[i - 1], hashes[i]);
    }
  }
}

}  // namespace test
}  // namespace webrtc
