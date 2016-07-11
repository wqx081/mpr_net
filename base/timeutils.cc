#include "base/timeutils.h"
#include "net/checks.h"

#include <stdint.h>

#include <sys/time.h>

namespace base {

ClockInterface* g_clock = nullptr;

ClockInterface* SetClockForTesting(ClockInterface* clock) {
  ClockInterface* prev = g_clock;
  g_clock = clock;
  return prev;
}

uint64_t SystemTimeNanos() {
  int64_t ticks;
  struct timespec ts;
  // TODO(deadbeef): Do we need to handle the case when CLOCK_MONOTONIC is not
  // supported?
  clock_gettime(CLOCK_MONOTONIC, &ts);
  ticks = kNumNanosecsPerSec * static_cast<int64_t>(ts.tv_sec) +
          static_cast<int64_t>(ts.tv_nsec);
  return ticks;
}

int64_t SystemTimeMillis() {
  return static_cast<int64_t>(SystemTimeNanos() / kNumNanosecsPerMillisec);
}

uint64_t TimeNanos() {
  if (g_clock) {
    return g_clock->TimeNanos();
  }
  return SystemTimeNanos();
}

uint32_t Time32() {
  return static_cast<uint32_t>(TimeNanos() / kNumNanosecsPerMillisec);
}

int64_t TimeMillis() {
  return static_cast<int64_t>(TimeNanos() / kNumNanosecsPerMillisec);
}

uint64_t TimeMicros() {
  return static_cast<uint64_t>(TimeNanos() / kNumNanosecsPerMicrosec);
}

int64_t TimeAfter(int64_t elapsed) {
  MPR_DCHECK_GE(elapsed, 0);
  return TimeMillis() + elapsed;
}

int32_t TimeDiff32(uint32_t later, uint32_t earlier) {
  return later - earlier;
}

int64_t TimeDiff(int64_t later, int64_t earlier) {
  return later - earlier;
}

TimestampWrapAroundHandler::TimestampWrapAroundHandler()
    : last_ts_(0), num_wrap_(-1) {}

int64_t TimestampWrapAroundHandler::Unwrap(uint32_t ts) {
  if (num_wrap_ == -1) {
    last_ts_ = ts;
    num_wrap_ = 0;
    return ts;
  }

  if (ts < last_ts_) {
    if (last_ts_ >= 0xf0000000 && ts < 0x0fffffff)
      ++num_wrap_;
  } else if ((ts - last_ts_) > 0xf0000000) {
    // Backwards wrap. Unwrap with last wrap count and don't update last_ts_.
    return ts + ((num_wrap_ - 1) << 32);
  }

  last_ts_ = ts;
  return ts + (num_wrap_ << 32);
}

int64_t TmToSeconds(const std::tm& tm) {
  static short int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  static short int cumul_mdays[12] = {0,   31,  59,  90,  120, 151,
                                      181, 212, 243, 273, 304, 334};
  int year = tm.tm_year + 1900;
  int month = tm.tm_mon;
  int day = tm.tm_mday - 1;  // Make 0-based like the rest.
  int hour = tm.tm_hour;
  int min = tm.tm_min;
  int sec = tm.tm_sec;

  bool expiry_in_leap_year = (year % 4 == 0 &&
                              (year % 100 != 0 || year % 400 == 0));

  if (year < 1970)
    return -1;
  if (month < 0 || month > 11)
    return -1;
  if (day < 0 || day >= mdays[month] + (expiry_in_leap_year && month == 2 - 1))
    return -1;
  if (hour < 0 || hour > 23)
    return -1;
  if (min < 0 || min > 59)
    return -1;
  if (sec < 0 || sec > 59)
    return -1;

  day += cumul_mdays[month];

  // Add number of leap days between 1970 and the expiration year, inclusive.
  day += ((year / 4 - 1970 / 4) - (year / 100 - 1970 / 100) +
          (year / 400 - 1970 / 400));

  // We will have added one day too much above if expiration is during a leap
  // year, and expiration is in January or February.
  if (expiry_in_leap_year && month <= 2 - 1) // |month| is zero based.
    day -= 1;

  // Combine all variables into seconds from 1970-01-01 00:00 (except |month|
  // which was accumulated into |day| above).
  return (((static_cast<int64_t>
            (year - 1970) * 365 + day) * 24 + hour) * 60 + min) * 60 + sec;
}

} // namespace rtc
