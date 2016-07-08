#include "base/semaphore.h"
#include <errno.h>

#include "base/time.h"

namespace base {

Semaphore::Semaphore(int count) {
  const uintptr_t kSemaphoreAlignmentMask = sizeof(void*) - 1;
  CHECK_EQ(
      0, reinterpret_cast<uintptr_t>(&native_handle_) &
      kSemaphoreAlignmentMask);
  DCHECK(count >= 0);
  int result = sem_init(&native_handle_, 0, count);
  DCHECK_EQ(0, result);
  USE(result);
}


Semaphore::~Semaphore() {
  int result = sem_destroy(&native_handle_);
  DCHECK_EQ(0, result);
  USE(result);
}

void Semaphore::Signal() {
  int result = sem_post(&native_handle_);
  // This check may fail with <libc-2.21, which we use on the try bots, if the
  // semaphore is destroyed while sem_post is still executed. A work around is
  // to extend the lifetime of the semaphore.
  CHECK_EQ(0, result);
}


void Semaphore::Wait() {
  while (true) {
    int result = sem_wait(&native_handle_);
    if (result == 0) return;  // Semaphore was signalled.
    // Signal caused spurious wakeup.
    DCHECK_EQ(-1, result);
    DCHECK_EQ(EINTR, errno);
  }
}


bool Semaphore::WaitFor(const TimeDelta& rel_time) {
  // Compute the time for end of timeout.
  const Time time = Time::NowFromSystemTime() + rel_time;
  const struct timespec ts = time.ToTimespec();

  // Wait for semaphore signalled or timeout.
  while (true) {
    int result = sem_timedwait(&native_handle_, &ts);
    if (result == 0) return true;  // Semaphore was signalled.
//#if V8_LIBC_GLIBC && !V8_GLIBC_PREREQ(2, 4)
    if (result > 0) {
      // sem_timedwait in glibc prior to 2.3.4 returns the errno instead of -1.
      errno = result;
      result = -1;
    }
//#endif
    if (result == -1 && errno == ETIMEDOUT) {
      // Timed out while waiting for semaphore.
      return false;
    }
    // Signal caused spurious wakeup.
    DCHECK_EQ(-1, result);
    DCHECK_EQ(EINTR, errno);
  }
}

}  // namespace base
