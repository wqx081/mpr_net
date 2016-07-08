#include "net/sharedexclusivelock.h"

namespace net {

SharedExclusiveLock::SharedExclusiveLock()
    : shared_count_is_zero_(true, true),
      shared_count_(0) {
}

void SharedExclusiveLock::LockExclusive() {
  cs_exclusive_.Enter();
  shared_count_is_zero_.Wait(Event::kForever);
}

void SharedExclusiveLock::UnlockExclusive() {
  cs_exclusive_.Leave();
}

void SharedExclusiveLock::LockShared() {
base::CritScope exclusive_scope(&cs_exclusive_);
base::CritScope shared_scope(&cs_shared_);
  if (++shared_count_ == 1) {
    shared_count_is_zero_.Reset();
  }
}

void SharedExclusiveLock::UnlockShared() {
base::CritScope shared_scope(&cs_shared_);
  if (--shared_count_ == 0) {
    shared_count_is_zero_.Set();
  }
}

}  // namespace rtc
