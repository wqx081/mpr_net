#ifndef WEBRTC_BASE_EVENT_H__
#define WEBRTC_BASE_EVENT_H__

#include <pthread.h>

namespace net {

class Event {
 public:
  static const int kForever = -1;

  Event(bool manual_reset, bool initially_signaled);
  ~Event();

  void Set();
  void Reset();

  // Wait for the event to become signaled, for the specified number of
  // |milliseconds|.  To wait indefinetly, pass kForever.
  bool Wait(int milliseconds);

 private:
  pthread_mutex_t event_mutex_;
  pthread_cond_t event_cond_;
  const bool is_manual_reset_;
  bool event_status_;
};

}  // namespace rtc

#endif  // WEBRTC_BASE_EVENT_H__
