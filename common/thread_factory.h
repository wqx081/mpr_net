#ifndef COMMON_THREAD_FACTORY_H_
#define COMMON_THREAD_FACTORY_H_

#include "fb/executor.h"
#include <thread>

namespace fb {

class ThreadFactory {
 public:
  virtual ~ThreadFactory() {}
  virtual std::thread NewThread(Func&& func) = 0;
};

}
#endif // COMMON_THREAD_FACTORY_H_
