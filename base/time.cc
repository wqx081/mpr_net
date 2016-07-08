#include "base/time.h"

#include <time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <unistd.h>

#include <ostream>

#include <glog/logging.h>

namespace base {

TimeDelta TimeDelta::FromDays(int days) {
  return TimeDelta(days * Time::kMicrosecondsPerDay);
}


TimeDelta TimeDelta::FromHours(int hours) {
  return TimeDelta(hours * Time::kMicrosecondsPerHour);
}


TimeDelta TimeDelta::FromMinutes(int minutes) {
  return TimeDelta(minutes * Time::kMicrosecondsPerMinute);
}


TimeDelta TimeDelta::FromSeconds(int64_t seconds) {
  return TimeDelta(seconds * Time::kMicrosecondsPerSecond);
}


TimeDelta TimeDelta::FromMilliseconds(int64_t milliseconds) {
  return TimeDelta(milliseconds * Time::kMicrosecondsPerMillisecond);
}


TimeDelta TimeDelta::FromNanoseconds(int64_t nanoseconds) {
  return TimeDelta(nanoseconds / Time::kNanosecondsPerMicrosecond);
}


int TimeDelta::InDays() const {
  return static_cast<int>(delta_ / Time::kMicrosecondsPerDay);
}


int TimeDelta::InHours() const {
  return static_cast<int>(delta_ / Time::kMicrosecondsPerHour);
}


int TimeDelta::InMinutes() const {
  return static_cast<int>(delta_ / Time::kMicrosecondsPerMinute);
}


double TimeDelta::InSecondsF() const {
  return static_cast<double>(delta_) / Time::kMicrosecondsPerSecond;
}

int64_t TimeDelta::InSeconds() const {
  return delta_ / Time::kMicrosecondsPerSecond;
}


double TimeDelta::InMillisecondsF() const {
  return static_cast<double>(delta_) / Time::kMicrosecondsPerMillisecond;
}


int64_t TimeDelta::InMilliseconds() const {
  return delta_ / Time::kMicrosecondsPerMillisecond;
}


int64_t TimeDelta::InNanoseconds() const {
  return delta_ * Time::kNanosecondsPerMicrosecond;
}

TimeDelta TimeDelta::FromTimespec(struct timespec ts) {
  DCHECK_GE(ts.tv_nsec, 0);
  DCHECK_LT(ts.tv_nsec,
            static_cast<long>(Time::kNanosecondsPerSecond));  // NOLINT
  return TimeDelta(ts.tv_sec * Time::kMicrosecondsPerSecond +
                    ts.tv_nsec / Time::kNanosecondsPerMicrosecond);
}


struct timespec TimeDelta::ToTimespec() const {
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(delta_ / Time::kMicrosecondsPerSecond);
  ts.tv_nsec = (delta_ % Time::kMicrosecondsPerSecond) *
      Time::kNanosecondsPerMicrosecond;
  return ts;
}

/////////////////////////////////////////////

std::string Time::Format(const std::string format) const {
  char buf[64] = {0};
  struct timeval tv = ToTimeval();  
  //struct tm* tmp = gmtime(&(tv.tv_sec));
  struct tm* tmp = std::localtime(&(tv.tv_sec));

  DCHECK(tmp != nullptr);  
  int n = strftime(buf, 64, format.c_str(), tmp);
  DCHECK(n > 0 && n < 64);
  return std::string(buf, n);
}

Time Time::Now() {
  struct timeval tv;
  int result = gettimeofday(&tv, nullptr);
  DCHECK_EQ(0, result);
  return FromTimeval(tv);
}

Time Time::NowFromSystemTime() {
  return Now();
}


Time Time::FromTimespec(struct timespec ts) {
  DCHECK(ts.tv_nsec >= 0);
  DCHECK(ts.tv_nsec < static_cast<long>(kNanosecondsPerSecond));  // NOLINT
  if (ts.tv_nsec == 0 && ts.tv_sec == 0) {
    return Time();
  }
  if (ts.tv_nsec == static_cast<long>(kNanosecondsPerSecond - 1) &&  // NOLINT
      ts.tv_sec == std::numeric_limits<time_t>::max()) {
    return Max();
  }
  return Time(ts.tv_sec * kMicrosecondsPerSecond +
              ts.tv_nsec / kNanosecondsPerMicrosecond);
}

struct timespec Time::ToTimespec() const {
  struct timespec ts;
  if (IsNull()) {
    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    return ts;
  }
  if (IsMax()) {
    ts.tv_sec = std::numeric_limits<time_t>::max();
    ts.tv_nsec = static_cast<long>(kNanosecondsPerSecond - 1);  // NOLINT
    return ts;
  }
  ts.tv_sec = static_cast<time_t>(us_ / kMicrosecondsPerSecond);
  ts.tv_nsec = (us_ % kMicrosecondsPerSecond) * kNanosecondsPerMicrosecond;
  return ts;
}

Time Time::FromTimeval(struct timeval tv) {
  DCHECK(tv.tv_usec >= 0);
  DCHECK(tv.tv_usec < static_cast<suseconds_t>(kMicrosecondsPerSecond));
  if (tv.tv_usec == 0 && tv.tv_sec == 0) {
    return Time();
  }
  if (tv.tv_usec == static_cast<suseconds_t>(kMicrosecondsPerSecond - 1) &&
            tv.tv_sec == std::numeric_limits<time_t>::max()) {
    return Max();
  }
  return Time(tv.tv_sec * kMicrosecondsPerSecond + tv.tv_usec);
}

struct timeval Time::ToTimeval() const {
  struct timeval tv;
  if (IsNull()) {
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    return tv;
  }
  if (IsMax()) {
    tv.tv_sec = std::numeric_limits<time_t>::max();
    tv.tv_usec = static_cast<suseconds_t>(kMicrosecondsPerSecond - 1);
    return tv;
  }
  tv.tv_sec = static_cast<time_t>(us_ / kMicrosecondsPerSecond);
  tv.tv_usec = us_ % kMicrosecondsPerSecond;
  return tv;
}

////////////////////////////
TimeTicks TimeTicks::Now() {
  return HighResolutionNow();
}

TimeTicks TimeTicks::HighResolutionNow() {
  int64_t ticks;
  struct timespec ts;
  int result = clock_gettime(CLOCK_MONOTONIC, &ts);
  DCHECK_EQ(0, result);
  //USE(result);
  ticks = (ts.tv_sec * Time::kMicrosecondsPerSecond +
           ts.tv_nsec / Time::kNanosecondsPerMicrosecond);
  return TimeTicks(ticks + 1);
}

bool TimeTicks::IsHighResolutionClockWorking() {
  return true;
}

std::ostream& operator<<(std::ostream& os, const Time& time) {
  return os << time.Format("%Y-%m-%d %H:%M:%S");
}

} // namespace base
