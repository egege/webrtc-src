/*
 *  Copyright (c) 2021 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */
#include "net/dcsctp/timer/timer.h"

#include <cmath>
#include <memory>
#include <optional>

#include "api/task_queue/task_queue_base.h"
#include "api/units/time_delta.h"
#include "api/units/timestamp.h"
#include "net/dcsctp/public/types.h"
#include "net/dcsctp/timer/fake_timeout.h"
#include "test/gmock.h"
#include "test/gtest.h"

namespace dcsctp {
namespace {
using ::testing::Return;
using ::webrtc::TimeDelta;
using ::webrtc::Timestamp;

class TimerTest : public testing::Test {
 protected:
  TimerTest()
      : timeout_manager_([this]() { return now_; }),
        manager_([this](webrtc::TaskQueueBase::DelayPrecision precision) {
          return timeout_manager_.CreateTimeout(precision);
        }) {
    ON_CALL(on_expired_, Call).WillByDefault(Return(TimeDelta::Zero()));
  }

  void AdvanceTimeAndRunTimers(TimeDelta duration) {
    now_ = now_ + duration;

    for (;;) {
      std::optional<TimeoutID> timeout_id =
          timeout_manager_.GetNextExpiredTimeout();
      if (!timeout_id.has_value()) {
        break;
      }
      manager_.HandleTimeout(*timeout_id);
    }
  }

  Timestamp now_ = Timestamp::Zero();
  FakeTimeoutManager timeout_manager_;
  TimerManager manager_;
  testing::MockFunction<TimeDelta()> on_expired_;
};

TEST_F(TimerTest, TimerIsInitiallyStopped) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kFixed));

  EXPECT_FALSE(t1->is_running());
}

TEST_F(TimerTest, TimerExpiresAtGivenTime) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kFixed));

  EXPECT_CALL(on_expired_, Call).Times(0);
  t1->Start();
  EXPECT_TRUE(t1->is_running());

  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
}

TEST_F(TimerTest, TimerReschedulesAfterExpiredWithFixedBackoff) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kFixed));

  EXPECT_CALL(on_expired_, Call).Times(0);
  t1->Start();
  EXPECT_EQ(t1->expiration_count(), 0);

  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Fire first time
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_TRUE(t1->is_running());
  EXPECT_EQ(t1->expiration_count(), 1);

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Second time
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_TRUE(t1->is_running());
  EXPECT_EQ(t1->expiration_count(), 2);

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Third time
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_TRUE(t1->is_running());
  EXPECT_EQ(t1->expiration_count(), 3);
}

TEST_F(TimerTest, TimerWithNoRestarts) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kFixed,
                   /*max_restart=*/0));

  EXPECT_CALL(on_expired_, Call).Times(0);
  t1->Start();
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Fire first time
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));

  EXPECT_FALSE(t1->is_running());

  // Second time - shouldn't fire
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(5));
  EXPECT_FALSE(t1->is_running());
}

TEST_F(TimerTest, TimerWithOneRestart) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kFixed,
                   /*max_restart=*/1));

  EXPECT_CALL(on_expired_, Call).Times(0);
  t1->Start();
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Fire first time
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_TRUE(t1->is_running());

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Second time - max restart limit reached.
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_FALSE(t1->is_running());

  // Third time - should not fire.
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(5));
  EXPECT_FALSE(t1->is_running());
}

TEST_F(TimerTest, TimerWithTwoRestart) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kFixed,
                   /*max_restart=*/2));

  EXPECT_CALL(on_expired_, Call).Times(0);
  t1->Start();
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Fire first time
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_TRUE(t1->is_running());

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Second time
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_TRUE(t1->is_running());

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Third time
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_FALSE(t1->is_running());
}

TEST_F(TimerTest, TimerWithExponentialBackoff) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kExponential));

  t1->Start();

  // Fire first time at 5 seconds
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(5));

  // Second time at 5*2^1 = 10 seconds later.
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(9));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));

  // Third time at 5*2^2 = 20 seconds later.
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(19));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));

  // Fourth time at 5*2^3 = 40 seconds later.
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(39));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
}

TEST_F(TimerTest, StartTimerWillStopAndStart) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kExponential));

  t1->Start();

  AdvanceTimeAndRunTimers(TimeDelta::Seconds(3));

  t1->Start();

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(2));

  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(3));
}

TEST_F(TimerTest, ExpirationCounterWillResetIfStopped) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kExponential));

  t1->Start();

  // Fire first time at 5 seconds
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(5));
  EXPECT_EQ(t1->expiration_count(), 1);

  // Second time at 5*2^1 = 10 seconds later.
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(9));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_EQ(t1->expiration_count(), 2);

  t1->Start();
  EXPECT_EQ(t1->expiration_count(), 0);

  // Third time at 5*2^0 = 5 seconds later.
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_EQ(t1->expiration_count(), 1);
}

TEST_F(TimerTest, StopTimerWillMakeItNotExpire) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kExponential));

  t1->Start();
  EXPECT_TRUE(t1->is_running());

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));
  t1->Stop();
  EXPECT_FALSE(t1->is_running());

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
}

TEST_F(TimerTest, ReturningNewDurationWhenExpired) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(5), TimerBackoffAlgorithm::kFixed));

  EXPECT_CALL(on_expired_, Call).Times(0);
  t1->Start();
  EXPECT_EQ(t1->duration(), TimeDelta::Seconds(5));

  AdvanceTimeAndRunTimers(TimeDelta::Seconds(4));

  // Fire first time
  EXPECT_CALL(on_expired_, Call).WillOnce(Return(TimeDelta::Seconds(2)));
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_EQ(t1->duration(), TimeDelta::Seconds(2));

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));

  // Second time
  EXPECT_CALL(on_expired_, Call).WillOnce(Return(TimeDelta::Seconds(10)));
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
  EXPECT_EQ(t1->duration(), TimeDelta::Seconds(10));

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(9));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));
}

TEST_F(TimerTest, TimersHaveMaximumDuration) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(1), TimerBackoffAlgorithm::kExponential));

  t1->set_duration(2 * Timer::kMaxTimerDuration);
  EXPECT_EQ(t1->duration(), Timer::kMaxTimerDuration);
}

TEST_F(TimerTest, TimersHaveMaximumBackoffDuration) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(1), TimerBackoffAlgorithm::kExponential));

  t1->Start();

  int max_exponent = static_cast<int>(log2(Timer::kMaxTimerDuration.seconds()));
  for (int i = 0; i < max_exponent; ++i) {
    EXPECT_CALL(on_expired_, Call).Times(1);
    AdvanceTimeAndRunTimers(TimeDelta::Seconds(1 * (1 << i)));
  }

  // Reached the maximum duration.
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(Timer::kMaxTimerDuration);

  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(Timer::kMaxTimerDuration);

  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(Timer::kMaxTimerDuration);

  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(Timer::kMaxTimerDuration);
}

TEST_F(TimerTest, TimerCanBeStartedFromWithinExpirationHandler) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(1), TimerBackoffAlgorithm::kFixed));

  t1->Start();

  // Start a timer, but don't return any new duration in callback.
  EXPECT_CALL(on_expired_, Call).WillOnce([&]() {
    EXPECT_TRUE(t1->is_running());
    t1->set_duration(TimeDelta::Seconds(5));
    t1->Start();
    return TimeDelta::Zero();
  });
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(4999));

  // Start a timer, and return any new duration in callback.
  EXPECT_CALL(on_expired_, Call).WillOnce([&]() {
    EXPECT_TRUE(t1->is_running());
    t1->set_duration(TimeDelta::Seconds(5));
    t1->Start();
    return TimeDelta::Seconds(8);
  });
  AdvanceTimeAndRunTimers(TimeDelta::Millis(1));

  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(7999));

  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(1));
}

TEST_F(TimerTest, DurationStaysWithinMaxTimerBackOffDuration) {
  std::unique_ptr<Timer> t1 = manager_.CreateTimer(
      "t1", on_expired_.AsStdFunction(),
      TimerOptions(TimeDelta::Seconds(1), TimerBackoffAlgorithm::kExponential,
                   /*max_restarts=*/std::nullopt, TimeDelta::Seconds(5)));

  t1->Start();

  // Initial timeout, 1000 ms
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Seconds(1));

  // Exponential backoff -> 2000 ms
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(1999));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(1));

  // Exponential backoff -> 4000 ms
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(3999));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(1));

  // Limited backoff -> 5000ms
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(4999));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(1));

  // ... where it plateaus
  EXPECT_CALL(on_expired_, Call).Times(0);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(4999));
  EXPECT_CALL(on_expired_, Call).Times(1);
  AdvanceTimeAndRunTimers(TimeDelta::Millis(1));
}

TEST(TimerManagerTest, TimerManagerPassesPrecisionToCreateTimeoutMethod) {
  FakeTimeoutManager timeout_manager([&]() { return Timestamp::Zero(); });
  std::optional<webrtc::TaskQueueBase::DelayPrecision> create_timer_precison;
  TimerManager manager([&](webrtc::TaskQueueBase::DelayPrecision precision) {
    create_timer_precison = precision;
    return timeout_manager.CreateTimeout(precision);
  });
  // Default TimerOptions.
  manager.CreateTimer(
      "test_timer", []() { return TimeDelta::Zero(); },
      TimerOptions(TimeDelta::Millis(123)));
  EXPECT_EQ(create_timer_precison, webrtc::TaskQueueBase::DelayPrecision::kLow);
  // High precision TimerOptions.
  manager.CreateTimer(
      "test_timer", []() { return TimeDelta::Zero(); },
      TimerOptions(TimeDelta::Millis(123), TimerBackoffAlgorithm::kExponential,
                   std::nullopt, TimeDelta::PlusInfinity(),
                   webrtc::TaskQueueBase::DelayPrecision::kHigh));
  EXPECT_EQ(create_timer_precison,
            webrtc::TaskQueueBase::DelayPrecision::kHigh);
  // Low precision TimerOptions.
  manager.CreateTimer(
      "test_timer", []() { return TimeDelta::Zero(); },
      TimerOptions(TimeDelta::Millis(123), TimerBackoffAlgorithm::kExponential,
                   std::nullopt, TimeDelta::PlusInfinity(),
                   webrtc::TaskQueueBase::DelayPrecision::kLow));
  EXPECT_EQ(create_timer_precison, webrtc::TaskQueueBase::DelayPrecision::kLow);
}

}  // namespace
}  // namespace dcsctp
