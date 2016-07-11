#ifndef FB_TIMEOUT_MANAGER_H_
#define FB_TIMEOUT_MANAGER_H_
#include <chrono>
#include <stdint.h>

namespace fb {

class AsyncTimeout;

class TimeoutManager {
 public:
  enum class InternalEnum {
    INTERNAL,
    NORMAL
  };
  virtual ~TimeoutManager() {}

  virtual void AttachTimeoutManager(AsyncTimeout* obj,
                                    InternalEnum internal) = 0;
  virtual void DetachTimeoutManager(AsyncTimeout* obj) = 0;
  virtual bool ScheduleTimeout(AsyncTimeout* obj,
                               std::chrono::milliseconds timeout) = 0;
  virtual void CancelTimeout(AsyncTimeout* obj) = 0;
  virtual bool BumpHandlingTime() = 0;
  virtual bool IsInTimeoutManagerThread() = 0;
};

} // namespace fb
#endif // FB_TIMEOUT_MANAGER_H_
