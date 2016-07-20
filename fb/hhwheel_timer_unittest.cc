#include "fb/hhwheel_timer.h"
#include "fb/event_base.h"

#include "fb/test/undelayed_destruction.h"
#include "fb/test/util.h"

#include <gtest/gtest.h>
#include <vector>

using namespace fb;
using std::chrono::milliseconds;

typedef UndelayedDestruction<HHWheelTimer> StackWheelTimer;

class TestTimeout : public HHWheelTimer::Callback {
 public:
  TestTimeout() {}
  TestTimeout(HHWheelTimer* t, milliseconds timeout) {
    t->ScheduleTimeout(this, timeout);
  }

  void TimeoutExpired() noexcept override {
    timestamps.push_back(TimePoint());
    if (fn) {
      fn();
    }
  }

  void CallbackCanceled() noexcept override {
    canceled_timestamps.push_back(TimePoint());
    if (fn) {
      fn();
    }
  }

  std::deque<TimePoint> timestamps;
  std::deque<TimePoint> canceled_timestamps;
  std::function<void()> fn;
};

class TestTimeoutDelayed : public TestTimeout {
 protected:
  std::chrono::milliseconds GetCurrentTime() override {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()) -
        milliseconds(5);
  }
};

struct HHWheelTimerTest : public ::testing::Test {
 EventBase event_base;
};


/*
 * Test firing some simple timeouts that are fired once and never rescheduled
 */

TEST_F(HHWheelTimerTest, FireOnce) {
  StackWheelTimer t(&event_base, milliseconds(1));

//  const HHWheelTimer::Callback* null_callback = nullptr;

  TestTimeout t1;
  TestTimeout t2;
  TestTimeout t3;

  ASSERT_EQ(t.Count(), 0);

  t.ScheduleTimeout(&t1, milliseconds(5));
  t.ScheduleTimeout(&t2, milliseconds(5));
  t.ScheduleTimeout(&t2, milliseconds(5));
  t.ScheduleTimeout(&t3, milliseconds(10));

  ASSERT_EQ(t.Count(), 3);

  TimePoint start;
  event_base.Loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t3.timestamps.size(), 1);

  ASSERT_EQ(t.Count(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(start, t2.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(start, t3.timestamps[0], milliseconds(10));
  T_CHECK_TIMEOUT(start, end, milliseconds(10));
}

/*
 * Test scheduling a timeout from another timeout callback.
 */
TEST_F(HHWheelTimerTest, TestSchedulingWithinCallback) {
  StackWheelTimer t(&event_base, milliseconds(10));
  //const HHWheelTimer::Callback* nullCallback = nullptr;

  TestTimeout t1;
  TestTimeoutDelayed t2;

  t.ScheduleTimeout(&t1, milliseconds(500));
  t1.fn = [&] { t.ScheduleTimeout(&t2, milliseconds(1)); };
  t2.fn = [&] { t.DetachEventBase(); };

  ASSERT_EQ(t.Count(), 1);

  event_base.Loop();

  ASSERT_EQ(t.Count(), 0);
  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t2.timestamps.size(), 1);
}

/*
 * Test cancelling a timeout when it is scheduled to be fired right away.
 */
TEST_F(HHWheelTimerTest, CancelTimeout) {
  StackWheelTimer t(&event_base, milliseconds(1));

  TestTimeout t5_1(&t, milliseconds(5));
  TestTimeout t5_2(&t, milliseconds(5));
  TestTimeout t5_3(&t, milliseconds(5));
  TestTimeout t5_4(&t, milliseconds(5));
  TestTimeout t5_5(&t, milliseconds(5));

  TestTimeout t10_1(&t, milliseconds(10));
  TestTimeout t10_2(&t, milliseconds(10));
  TestTimeout t10_3(&t, milliseconds(10));

  TestTimeout t20_1(&t, milliseconds(20));
  TestTimeout t20_2(&t, milliseconds(20));

  t5_1.fn = [&] {
    t5_2.CancelTimeout();
    t5_4.CancelTimeout();
  };

  t5_3.fn = [&] {
    t5_5.CancelTimeout();
    std::function<void()> fnDtorGuard;
    t5_3.fn.swap(fnDtorGuard);
    t.ScheduleTimeout(&t5_3, milliseconds(5));

    t10_2.CancelTimeout();
    t20_1.CancelTimeout();
    t20_2.CancelTimeout();
  };

  TimePoint start;
  event_base.Loop();
  TimePoint end;

  ASSERT_EQ(t5_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_1.timestamps[0], milliseconds(5));
  ASSERT_EQ(t5_3.timestamps.size(), 2);
  T_CHECK_TIMEOUT(start, t5_3.timestamps[0], milliseconds(5));
  T_CHECK_TIMEOUT(t5_3.timestamps[0], t5_3.timestamps[1], milliseconds(5));
  ASSERT_EQ(t10_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t10_1.timestamps[0], milliseconds(10));
  ASSERT_EQ(t10_3.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t10_3.timestamps[0], milliseconds(10));

  ASSERT_EQ(t5_2.timestamps.size(), 0);
  ASSERT_EQ(t5_4.timestamps.size(), 0);
  ASSERT_EQ(t5_5.timestamps.size(), 0);
  ASSERT_EQ(t10_2.timestamps.size(), 0);
  ASSERT_EQ(t20_1.timestamps.size(), 0);
  ASSERT_EQ(t20_2.timestamps.size(), 0);
  T_CHECK_TIMEOUT(start, end, milliseconds(10));
}

/*
 * Test destroying a HHWheelTimer with timeouts outstanding
 */
TEST_F(HHWheelTimerTest, DestroyTimeoutSet) {

  HHWheelTimer::UniquePtr t(
    new HHWheelTimer(&event_base, milliseconds(1)));

  TestTimeout t5_1(t.get(), milliseconds(5));
  TestTimeout t5_2(t.get(), milliseconds(5));
  TestTimeout t5_3(t.get(), milliseconds(5));

  TestTimeout t10_1(t.get(), milliseconds(10));
  TestTimeout t10_2(t.get(), milliseconds(10));

  t5_2.fn = [&] {
    t5_3.CancelTimeout();
    t5_1.CancelTimeout();
    t10_1.CancelTimeout();
    t10_2.CancelTimeout();
    t.reset();};

  TimePoint start;
  event_base.Loop();
  TimePoint end;

  ASSERT_EQ(t5_1.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_1.timestamps[0], milliseconds(5));
  ASSERT_EQ(t5_2.timestamps.size(), 1);
  T_CHECK_TIMEOUT(start, t5_2.timestamps[0], milliseconds(5));

  ASSERT_EQ(t5_3.timestamps.size(), 0);
  ASSERT_EQ(t10_1.timestamps.size(), 0);
  ASSERT_EQ(t10_2.timestamps.size(), 0);

  T_CHECK_TIMEOUT(start, end, milliseconds(5));
}

TEST_F(HHWheelTimerTest, AtMostEveryN) {

  milliseconds interval(25);
  milliseconds atMostEveryN(6);
  StackWheelTimer t(&event_base, atMostEveryN);
  t.SetCatchupEveryN(70);

  uint32_t numTimeouts = 60;
  std::vector<TestTimeout> timeouts(numTimeouts);

  uint32_t index = 0;
  StackWheelTimer ts1(&event_base, milliseconds(1));
  TestTimeout scheduler(&ts1, milliseconds(1));
  scheduler.fn = [&] {
    if (index >= numTimeouts) {
      return;
    }
    timeouts[index].TimeoutExpired();
    t.ScheduleTimeout(&timeouts[index], interval);
    ts1.ScheduleTimeout(&scheduler, milliseconds(1));
    ++index;
  };
  TimePoint start;
  event_base.Loop();
  TimePoint end;
  for (uint32_t idx = 0; idx < numTimeouts; ++idx) {
    ASSERT_EQ(timeouts[idx].timestamps.size(), 2);
    TimePoint scheduledTime(timeouts[idx].timestamps[0]);
    TimePoint firedTime(timeouts[idx].timestamps[1]);
    milliseconds tolerance = milliseconds(5) + interval;
    T_CHECK_TIMEOUT(scheduledTime, firedTime, atMostEveryN, tolerance);
    if (idx == 0) {
      continue;
    }
    TimePoint prev(timeouts[idx - 1].timestamps[1]);
    auto delta = (firedTime.GetTimeStart() - prev.GetTimeEnd()) -
      (firedTime.GetTimeWaiting() - prev.GetTimeWaiting());
    if (delta > milliseconds(1)) {
      T_CHECK_TIMEOUT(prev, firedTime, atMostEveryN);
    }
  }
}

TEST_F(HHWheelTimerTest, SlowLoop) {
  StackWheelTimer t(&event_base, milliseconds(1));

  TestTimeout t1;
  TestTimeout t2;

  ASSERT_EQ(t.Count(), 0);

  event_base.RunInLoop([](){usleep(10000);});
  t.ScheduleTimeout(&t1, milliseconds(5));

  ASSERT_EQ(t.Count(), 1);

  TimePoint start;
  event_base.Loop();
  TimePoint end;

  ASSERT_EQ(t1.timestamps.size(), 1);
  ASSERT_EQ(t.Count(), 0);

  T_CHECK_TIMEOUT(start, t1.timestamps[0], milliseconds(15), milliseconds(1));
  T_CHECK_TIMEOUT(start, end, milliseconds(15), milliseconds(1));

  t.SetCatchupEveryN(1);
  event_base.RunInLoop([](){usleep(10000);});
  t.ScheduleTimeout(&t2, milliseconds(5));

  ASSERT_EQ(t.Count(), 1);

  TimePoint start2;
  event_base.Loop();
  TimePoint end2;

  ASSERT_EQ(t2.timestamps.size(), 1);
  ASSERT_EQ(t.Count(), 0);

  T_CHECK_TIMEOUT(start2, t2.timestamps[0], milliseconds(10), milliseconds(1));
  T_CHECK_TIMEOUT(start2, end2, milliseconds(10), milliseconds(1));
}

TEST_F(HHWheelTimerTest, lambda) {
  StackWheelTimer t(&event_base, milliseconds(1));
  size_t count = 0;
  t.ScheduleTimeoutFn([&]{ count++; }, milliseconds(1));
  event_base.Loop();
  EXPECT_EQ(1, count);
}

TEST_F(HHWheelTimerTest, lambdaThrows) {
  StackWheelTimer t(&event_base, milliseconds(1));
  t.ScheduleTimeoutFn([&]{ throw std::runtime_error("expected"); },
  milliseconds(1));
  event_base.Loop();
}

TEST_F(HHWheelTimerTest, cancelAll) {
  StackWheelTimer t(&event_base);
  TestTimeout tt;
  t.ScheduleTimeout(&tt, std::chrono::minutes(1));
  EXPECT_EQ(1, t.CancelAll());
  EXPECT_EQ(1, tt.canceled_timestamps.size());
}


