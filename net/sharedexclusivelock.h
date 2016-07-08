#ifndef WEBRTC_BASE_SHAREDEXCLUSIVELOCK_H_
#define WEBRTC_BASE_SHAREDEXCLUSIVELOCK_H_

#include "base/macros.h"
#include "base/critical_section.h"
#include "net/event.h"

namespace net {

// This class provides shared-exclusive lock. It can be used in cases like
// multiple-readers/single-writer model.
class SharedExclusiveLock {
 public:
  SharedExclusiveLock();

  // Locking/unlocking methods. It is encouraged to use SharedScope or
  // ExclusiveScope for protection.
  void LockExclusive(); // EXCLUSIVE_LOCK_FUNCTION();
  void UnlockExclusive(); // UNLOCK_FUNCTION();
  void LockShared();
  void UnlockShared();

 private:
  base::CriticalSection cs_exclusive_;
  base::CriticalSection cs_shared_;
  Event shared_count_is_zero_;
  int shared_count_;

  DISALLOW_COPY_AND_ASSIGN(SharedExclusiveLock);
};

class SharedScope {
 public:
  explicit SharedScope(SharedExclusiveLock* lock) //SHARED_LOCK_FUNCTION(lock)
      : lock_(lock) {
    lock_->LockShared();
  }

  ~SharedScope()  { lock_->UnlockShared(); }

 private:
  SharedExclusiveLock* lock_;

  DISALLOW_COPY_AND_ASSIGN(SharedScope);
};

class ExclusiveScope {
 public:
  explicit ExclusiveScope(SharedExclusiveLock* lock)
      : lock_(lock) {
    lock_->LockExclusive();
  }

  ~ExclusiveScope() { lock_->UnlockExclusive(); }

 private:
  SharedExclusiveLock* lock_;

  DISALLOW_COPY_AND_ASSIGN(ExclusiveScope);
};

}  // namespace rtc

#endif  // WEBRTC_BASE_SHAREDEXCLUSIVELOCK_H_
