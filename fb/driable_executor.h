#ifndef FB_DRIVABLE_EXECUTOR_H_
#define FB_DRIVABLE_EXECUTOR_H_
#include "fb/executor.h"

namespace fb {

class DrivableExecutor : public virtual Executor {
 public:
  virtual ~DrivableExecutor() {}
  virtual void Drive() = 0;
};

} // namespace fb
#endif // FB_DRIVABLE_EXECUTOR_H_
