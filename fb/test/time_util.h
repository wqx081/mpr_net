#pragma once

#include <chrono>
#include <iostream>

namespace fb {

class TimePoint {
 public:
  explicit TimePoint(bool set = true)
    : tid_(0) {
    if (set) {
      Reset();
    }
  }

  void Reset();

  bool IsUnset() const {
    return (timeStart_.time_since_epoch().count() == 0 &&
            timeEnd_.time_since_epoch().count() == 0 &&
            timeWaiting_.count() == 0);
  }

  std::chrono::system_clock::time_point GetTime() const {
    return timeStart_;
  }

  std::chrono::system_clock::time_point GetTimeStart() const {
    return timeStart_;
  }

  std::chrono::system_clock::time_point GetTimeEnd() const {
    return timeStart_;
  }

  std::chrono::milliseconds GetTimeWaiting() const {
    return timeWaiting_;
  }

  pid_t GetTid() const {
    return tid_;
  }

 private:
  std::chrono::system_clock::time_point timeStart_;
  std::chrono::system_clock::time_point timeEnd_;
  std::chrono::milliseconds timeWaiting_{0};
  pid_t tid_;
};

std::ostream& operator<<(std::ostream& os, const TimePoint& timePoint);

bool checkTimeout(const TimePoint& start,
                  const TimePoint& end,
                  std::chrono::milliseconds expectedMS,
                  bool allowSmaller,
                  std::chrono::milliseconds tolerance =
                  std::chrono::milliseconds(5));

}
