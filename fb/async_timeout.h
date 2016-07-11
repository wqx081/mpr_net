#ifndef FB_ASYNC_TIMEOUT_H_
#define FB_ASYNC_TIMEOUT_H_
#include "base/macros.h"
#include "fb/timeout_manager.h"

#include <event.h>
#include <memory>

namespace fb {

class EventBase;
class RequestContext;
class TimeoutManager;

class AsyncTimeout {
 public:
  typedef TimeoutManager::InternalEnum InternalEnum;

  explicit AsyncTimeout(TimeoutManager* timeout_manager);
  explicit AsyncTimeout(EventBase* event_base);

  AsyncTimeout(TimeoutManager* timeout_manager, InternalEnum internal);
  AsyncTimeout(EventBase* event_base, InternalEnum internal);
  AsyncTimeout();
  virtual ~AsyncTimeout();

  virtual void TimeoutExpired() noexcept = 0;

  bool ScheduleTimeout(uint32_t milliseconds); 
  bool ScheduleTimeout(std::chrono::milliseconds timeout);

  void CancelTiemout();
  bool IsScheduled() const;
  
  void AttachTimeoutManager(TimeoutManager* timeout_manager,
                            InternalEnum internal = InternalEnum::NORMAL);
  void AttachEventBase(EventBase* event_base,
                       InternalEnum internal = InternalEnum::NORMAL);

  void DetachTimeoutManager();
  void DetachEventBase();

  const TimeoutManager* GetTimeoutManager() {
    return timeout_manager_;
  } 

  struct event* GetEvent() {
    return &event_;
  }

 private:
  static void LibeventCallback(int fd, short events, void* arg);
  struct event event_;
  TimeoutManager* timeout_manager_;

  DISALLOW_COPY_AND_ASSIGN(AsyncTimeout);
};

} // namespace fb
#endif // FB_ASYNC_TIMEOUT_H_
