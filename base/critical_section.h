#ifndef WEBRTC_BASE_CRITICALSECTION_H_
#define WEBRTC_BASE_CRITICALSECTION_H_

#include "base/macros.h"
#include "base/atomicops.h"
#include "net/checks.h"

#include <pthread.h>

#if CS_DEBUG_CHECKS
#define CS_DEBUG_CODE(x) x
#else  // !CS_DEBUG_CHECKS
#define CS_DEBUG_CODE(x)
#endif  // !CS_DEBUG_CHECKS

namespace base {

// Locking methods (Enter, TryEnter, Leave)are const to permit protecting
// members inside a const context without requiring mutable CriticalSections
// everywhere.
class CriticalSection {
 public:
  CriticalSection();
  ~CriticalSection();

  void Enter() const;
  bool TryEnter() const;
  void Leave() const;

  // Use only for RTC_DCHECKing.
  bool CurrentThreadIsOwner() const;
  // Use only for RTC_DCHECKing.
  bool IsLocked() const;

 private:
  mutable pthread_mutex_t mutex_;
  CS_DEBUG_CODE(mutable PlatformThreadRef thread_);
  CS_DEBUG_CODE(mutable int recursion_count_);
};

// CritScope, for serializing execution through a scope.
class CritScope {
 public:
  explicit CritScope(const CriticalSection* cs);
  ~CritScope();
 private:
  const CriticalSection* const cs_;
  DISALLOW_COPY_AND_ASSIGN(CritScope);
};

// Tries to lock a critical section on construction via
// CriticalSection::TryEnter, and unlocks on destruction if the
// lock was taken. Never blocks.
//
// IMPORTANT: Unlike CritScope, the lock may not be owned by this thread in
// subsequent code. Users *must* check locked() to determine if the
// lock was taken. If you're not calling locked(), you're doing it wrong!
class TryCritScope {
 public:
  explicit TryCritScope(const CriticalSection* cs);
  ~TryCritScope();
  bool locked() const __attribute__ ((__warn_unused_result__));
 private:
  const CriticalSection* const cs_;
  const bool locked_;
  CS_DEBUG_CODE(mutable bool lock_was_called_);
  DISALLOW_COPY_AND_ASSIGN(TryCritScope);
};

// A POD lock used to protect global variables. Do NOT use for other purposes.
// No custom constructor or private data member should be added.
class GlobalLockPod {
 public:
  void Lock();

  void Unlock();

  volatile int lock_acquired;
};

class GlobalLock : public GlobalLockPod {
 public:
  GlobalLock();
};

// GlobalLockScope, for serializing execution through a scope.
class GlobalLockScope {
 public:
  explicit GlobalLockScope(GlobalLockPod* lock);
  ~GlobalLockScope();
 private:
  GlobalLockPod* const lock_;
  DISALLOW_COPY_AND_ASSIGN(GlobalLockScope);
};

} // namespace rtc

#endif // WEBRTC_BASE_CRITICALSECTION_H_
