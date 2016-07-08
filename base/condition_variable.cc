#include "base/condition_variable.h"
#include <errno.h>
#include <time.h>
#include "base/time.h"

namespace base {

ConditionVariable::ConditionVariable() {
  pthread_condattr_t attr;
  int result = pthread_condattr_init(&attr);
  DCHECK_EQ(0, result);
  result = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  DCHECK_EQ(0, result);
  result = pthread_cond_init(&native_handle_, &attr);
  DCHECK_EQ(0, result);
  USE(result);
}

ConditionVariable::~ConditionVariable() {
  int result = pthread_cond_destroy(&native_handle_);
  DCHECK_EQ(0, result);
  USE(result);
}

void ConditionVariable::NotifyOne() {
  int result = pthread_cond_signal(&native_handle_);
  DCHECK_EQ(0, result);
  USE(result);
}

void ConditionVariable::NotifyAll() {
  int result = pthread_cond_broadcast(&native_handle_);
  DCHECK_EQ(0, result);
  USE(result);
}

void ConditionVariable::Wait(Mutex* mutex) {
  int result = pthread_cond_wait(&native_handle_,
		                 &mutex->native_handle());
  DCHECK_EQ(0, result);
  USE(result);
}

bool ConditionVariable::WaitFor(Mutex* mutex, const TimeDelta& rel_time) {
  struct timespec ts;
  int result;

  result = clock_gettime(CLOCK_MONOTONIC, &ts);
  DCHECK_EQ(0, result);
  Time now = Time::FromTimespec(ts);
  Time end_time = now + rel_time;
  DCHECK_GE(end_time, now);
  ts = end_time.ToTimespec();
  result = pthread_cond_timedwait(&native_handle_,
		                  &mutex->native_handle(), &ts);
  if (result == ETIMEDOUT) {
    return false;
  }
  DCHECK_EQ(0, result);
  return true;
}

} // namespace base
