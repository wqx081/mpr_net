// Borrowed from Chromium's src/base/threading/thread_checker_impl.cc.

#include "net/thread_checker_impl.h"
#include "net/platform_thread.h"

namespace net {

ThreadCheckerImpl::ThreadCheckerImpl() : valid_thread_(CurrentThreadRef()) {
}

ThreadCheckerImpl::~ThreadCheckerImpl() {
}

bool ThreadCheckerImpl::CalledOnValidThread() const {
  const PlatformThreadRef current_thread = CurrentThreadRef();
base::CritScope scoped_lock(&lock_);
  if (!valid_thread_)  // Set if previously detached.
    valid_thread_ = current_thread;
  return IsThreadRefEqual(valid_thread_, current_thread);
}

void ThreadCheckerImpl::DetachFromThread() {
base::CritScope scoped_lock(&lock_);
  valid_thread_ = 0;
}

}  // namespace rtc
