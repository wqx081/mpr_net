#ifndef COMMON_SEMAPHORE_H_
#define COMMON_SEMAPHORE_H_

#include "base/macros.h"
#include <semaphore.h>  // NOLINT

namespace fb {

// Semaphore
//
// A semaphore object is a synchronization object that maintains a count. The
// count is decremented each time a thread completes a wait for the semaphore
// object and incremented each time a thread signals the semaphore. When the
// count reaches zero,  threads waiting for the semaphore blocks until the
// count becomes non-zero.

class Semaphore final {
 public:
  Semaphore();
  explicit Semaphore(int count);
  ~Semaphore();

  // Increments the semaphore counter.
  void Post();

  // Suspends the calling thread until the semaphore counter is non zero
  // and then decrements the semaphore counter.
  void Wait();

  // Suspends the calling thread until the counter is non zero or the timeout
  // time has passed. If timeout happens the return value is false and the
  // counter is unchanged. Otherwise the semaphore counter is decremented and
  // true is returned.
//  bool WaitFor(const TimeDelta& rel_time) ;

  typedef sem_t NativeHandle;

  NativeHandle& native_handle() {
    return native_handle_;
  }
  const NativeHandle& native_handle() const {
    return native_handle_;
  }

 private:
  NativeHandle native_handle_;

  DISALLOW_COPY_AND_ASSIGN(Semaphore);
};

}  // namespace base
#endif  // V8_BASE_PLATFORM_SEMAPHORE_H_
