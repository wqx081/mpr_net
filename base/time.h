#ifndef ALPHA_BASE_TIME_H_
#define ALPHA_BASE_TIME_H_

#include <ctime>
#include <iosfwd>
#include <limits>
#include <stdint.h>
#include "base/macros.h"

namespace base {


class Time;
class TimeTicks;

// TimeDelta
//
// This class represents a duration of time, internally represented in
// microseconds.
//
class TimeDelta final {
 public:
  TimeDelta() : delta_(0) {}

  static TimeDelta FromDays(int days);
  static TimeDelta FromHours(int hours);
  static TimeDelta FromMinutes(int minutes);
  static TimeDelta FromSeconds(int64_t seconds);
  static TimeDelta FromMilliseconds(int64_t milliseconds);
  static TimeDelta FromMicroseconds(int64_t microseconds) {
    return TimeDelta(microseconds);
  }
  static TimeDelta FromNanoseconds(int64_t nanoseconds);

  int InDays() const;
  int InHours() const;
  int InMinutes() const;
  double InSecondsF() const;
  int64_t InSeconds() const;
  double InMillisecondsF() const;
  int64_t InMilliseconds() const;
  int64_t InMicroseconds() const { return delta_; }
  int64_t InNanoseconds() const;

  // POSIX time specs.
  static TimeDelta FromTimespec(struct timespec ts);
  struct timespec ToTimespec() const;

  TimeDelta& operator=(const TimeDelta& other) {
    delta_ = other.delta_;
    return *this;
  }
  
  TimeDelta operator+(const TimeDelta& other) const {
    return TimeDelta(delta_ + other.delta_);
  }
  TimeDelta operator-(const TimeDelta& other) const {
    return TimeDelta(delta_ - other.delta_);
  }

  TimeDelta& operator+=(const TimeDelta& other) {
    delta_ += other.delta_;
    return *this;
  }
  TimeDelta& operator-=(const TimeDelta& other) {
    delta_ -= other.delta_;
    return *this;
  }
  TimeDelta operator-() const {
    return TimeDelta(-delta_);
  }

  double TimesOf(const TimeDelta& other) const {
    return static_cast<double>(delta_) / static_cast<double>(other.delta_);
  }
  double PercentOf(const TimeDelta& other) const {
    return TimesOf(other) * 100.0;
  }

  TimeDelta operator*(int64_t a) const {
    return TimeDelta(delta_ * a);
  }
  TimeDelta operator/(int64_t a) const {
    return TimeDelta(delta_ / a);
  }
  TimeDelta& operator*=(int64_t a) {
    delta_ *= a;
    return *this;
  }
  TimeDelta& operator/=(int64_t a) {
    delta_ /= a;
    return *this;
  }
  int64_t operator/(const TimeDelta& other) const {
    return delta_ / other.delta_;
  }

  bool operator==(const TimeDelta& other) const {
    return delta_ == other.delta_;
  }
  bool operator!=(const TimeDelta& other) const {
    return delta_ != other.delta_;
  }
  bool operator<(const TimeDelta& other) const {
    return delta_ < other.delta_;
  }
  bool operator<=(const TimeDelta& other) const {
    return delta_ <= other.delta_;
  }
  bool operator>(const TimeDelta& other) const {
    return delta_ > other.delta_;
  }
  bool operator>=(const TimeDelta& other) const {
    return delta_ >= other.delta_;
  }

 private:
  explicit TimeDelta(int64_t delta) : delta_(delta) {}

  // Delta in microseconds.
  int64_t delta_;
};

// Time
//
// This class represents an absolute point in time, internally represented as
// microseconds (s/1,000,000) since 00:00:00: UTC, Janunary 1, 1970.

class Time final {
 public:
  static const int64_t kMillisecondsPerSecond = 1000;
  static const int64_t kMicrosecondsPerMillisecond = 1000;
  static const int64_t kMicrosecondsPerSecond = kMicrosecondsPerMillisecond *
                                                kMillisecondsPerSecond;
  static const int64_t kMicrosecondsPerMinute = kMicrosecondsPerSecond * 60;
  static const int64_t kMicrosecondsPerHour = kMicrosecondsPerMinute * 60;
  static const int64_t kMicrosecondsPerDay = kMicrosecondsPerHour * 24;
  static const int64_t kMicrosecondsPerWeek = kMicrosecondsPerDay * 7;
  static const int64_t kNanosecondsPerMicrosecond = 1000;
  static const int64_t kNanosecondsPerSecond = kNanosecondsPerMicrosecond *
                                               kMicrosecondsPerSecond;

  Time() : us_(0) {}
  bool IsNull() const { return us_ == 0; }
  bool IsMax() const { return us_ == std::numeric_limits<int64_t>::max(); }
  
  static Time Now();

  std::string Format(const std::string format) const;

  static Time NowFromSystemTime();
  static Time UnixEpoch() { return Time(0); }
  
  static Time Max() { return Time(std::numeric_limits<int64_t>::max()); }
  static Time FromInternalValue(int64_t value) {
    return Time(value);
  }

  // POSIX
  static Time FromTimespec(struct timespec ts);
  struct timespec ToTimespec() const;

  static Time FromTimeval(struct timeval tv);
  struct timeval ToTimeval() const;

  Time& operator=(const Time& other) {
    us_ = other.us_;
    return *this;
  }
  TimeDelta operator-(const Time& other) const {
    return TimeDelta::FromMicroseconds(us_ - other.us_);
  }

  Time& operator+=(const TimeDelta& delta) {
    us_ += delta.InMicroseconds();
    return *this;
  }
  Time& operator-=(const TimeDelta& delta) {
    us_ -= delta.InMicroseconds();
    return *this;
  }

  Time operator+(const TimeDelta& delta) const {
    return Time(us_ + delta.InMicroseconds());
  }
  Time operator-(const TimeDelta& delta) const {
    return Time(us_ - delta.InMicroseconds());
  }

  bool operator==(const Time& other) const {
    return us_ == other.us_;
  }
  bool operator!=(const Time& other) const {
    return us_ != other.us_;
  }
  bool operator<(const Time& other) const {
    return us_ < other.us_;
  }
  bool operator<=(const Time& other) const {
    return us_ <= other.us_;
  }
  bool operator>(const Time& other) const {
    return us_ > other.us_;
  }
  bool operator>=(const Time& other) const {
    return us_ >= other.us_;
  }

 private:
  explicit Time(int64_t us) : us_(us) {}

  int64_t us_;
};

std::ostream& operator<<(std::ostream&, const Time&);

inline Time operator+(const TimeDelta& delta, const Time& time) {
  return time + delta;
}


// -----------------------------------------------------------------------------
// TimeTicks
//
// This class represents an abstract time that is most of the time incrementing
// for use in measuring time durations. It is internally represented in
// microseconds.  It can not be converted to a human-readable time, but is
// guaranteed not to decrease (if the user changes the computer clock,
// Time::Now() may actually decrease or jump).  But note that TimeTicks may
// "stand still", for example if the computer suspended.

class TimeTicks final {
 public:
  TimeTicks() : ticks_(0) {}

  static TimeTicks Now();
  static TimeTicks HighResolutionNow();
  static bool IsHighResolutionClockWorking();
  bool IsNull() const { return ticks_ == 0; }

  static TimeTicks FromInternalValue(int64_t value) {
    return TimeTicks(value);
  }
  int64_t ToInternalValue() const {
    return ticks_;
  }

  TimeTicks& operator=(const TimeTicks other) {
    ticks_ = other.ticks_;
    return *this;
  }

  TimeDelta operator-(const TimeTicks other) const {
    return TimeDelta::FromMicroseconds(ticks_ - other.ticks_);
  }

  TimeTicks& operator+=(const TimeDelta& delta) {
    ticks_ += delta.InMicroseconds();
    return *this;
  }
  TimeTicks& operator-=(const TimeDelta& delta) {
    ticks_ -= delta.InMicroseconds();
    return *this;
  }

  TimeTicks operator+(const TimeDelta& delta) const {
    return TimeTicks(ticks_ + delta.InMicroseconds());
  }
  TimeTicks operator-(const TimeDelta& delta) const {
    return TimeTicks(ticks_ - delta.InMicroseconds());
  }

  bool operator==(const TimeTicks& other) const {
    return ticks_ == other.ticks_;
  }
  bool operator!=(const TimeTicks& other) const {
    return ticks_ != other.ticks_;
  }
  bool operator<(const TimeTicks& other) const {
    return ticks_ < other.ticks_;
  }
  bool operator<=(const TimeTicks& other) const {
    return ticks_ <= other.ticks_;
  }
  bool operator>(const TimeTicks& other) const {
    return ticks_ > other.ticks_;
  }
  bool operator>=(const TimeTicks& other) const {
    return ticks_ >= other.ticks_;
  }


 private:
  explicit TimeTicks(int64_t ticks) : ticks_(ticks) {}

  int64_t ticks_;
};


std::ostream& operator<<(std::ostream&, const Time&);

} // namespace base
#endif // BASE_TIME_H_
