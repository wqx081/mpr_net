#include "base/critical_section.h"

// TODO(tommi): Split this file up to per-platform implementation files.

namespace base {

CriticalSection::CriticalSection() {
  pthread_mutexattr_t mutex_attribute;
  pthread_mutexattr_init(&mutex_attribute);
  pthread_mutexattr_settype(&mutex_attribute, PTHREAD_MUTEX_RECURSIVE);
  pthread_mutex_init(&mutex_, &mutex_attribute);
  pthread_mutexattr_destroy(&mutex_attribute);
  CS_DEBUG_CODE(thread_ = 0);
  CS_DEBUG_CODE(recursion_count_ = 0);
}

CriticalSection::~CriticalSection() {
  pthread_mutex_destroy(&mutex_);
}

void CriticalSection::Enter() const {

  pthread_mutex_lock(&mutex_);
#if CS_DEBUG_CHECKS
  if (!recursion_count_) {
    RTC_MPR_DCHECK(!thread_);
    thread_ = CurrentThreadRef();
  } else {
    RTC_MPR_DCHECK(CurrentThreadIsOwner());
  }
  ++recursion_count_;
#endif
}

bool CriticalSection::TryEnter() const {
  if (pthread_mutex_trylock(&mutex_) != 0)
    return false;
#if CS_DEBUG_CHECKS
  if (!recursion_count_) {
    RTC_MPR_DCHECK(!thread_);
    thread_ = CurrentThreadRef();
  } else {
    RTC_MPR_DCHECK(CurrentThreadIsOwner());
  }
  ++recursion_count_;
#endif
  return true;
}

void CriticalSection::Leave() const {
  MPR_DCHECK(CurrentThreadIsOwner());
#if CS_DEBUG_CHECKS
  --recursion_count_;
  RTC_MPR_DCHECK(recursion_count_ >= 0);
  if (!recursion_count_)
    thread_ = 0;
#endif

  pthread_mutex_unlock(&mutex_);
}

bool CriticalSection::CurrentThreadIsOwner() const {
#if CS_DEBUG_CHECKS
  return IsThreadRefEqual(thread_, CurrentThreadRef());
#else
  return true;
#endif  // CS_DEBUG_CHECKS
}

bool CriticalSection::IsLocked() const {
#if CS_DEBUG_CHECKS
  return thread_ != 0;
#else
  return true;
#endif
}

CritScope::CritScope(const CriticalSection* cs) : cs_(cs) { cs_->Enter(); }
CritScope::~CritScope() { cs_->Leave(); }

TryCritScope::TryCritScope(const CriticalSection* cs)
    : cs_(cs), locked_(cs->TryEnter()) {
  CS_DEBUG_CODE(lock_was_called_ = false);
}

TryCritScope::~TryCritScope() {
  CS_DEBUG_CODE(RTC_MPR_DCHECK(lock_was_called_));
  if (locked_)
    cs_->Leave();
}

bool TryCritScope::locked() const {
  CS_DEBUG_CODE(lock_was_called_ = true);
  return locked_;
}

void GlobalLockPod::Lock() {
  //const struct timespec ts_null = {0};

  while (subtle::Acquire_CompareAndSwap(&lock_acquired, 0, 1)) {
    sched_yield();
  }
}

void GlobalLockPod::Unlock() {
  int old_value = subtle::Acquire_CompareAndSwap(&lock_acquired, 1, 0);
  MPR_DCHECK_EQ(1, old_value) << "Unlock called without calling Lock first";
}

GlobalLock::GlobalLock() {
  lock_acquired = 0;
}

GlobalLockScope::GlobalLockScope(GlobalLockPod* lock)
    : lock_(lock) {
  lock_->Lock();
}

GlobalLockScope::~GlobalLockScope() {
  lock_->Unlock();
}

}  // namespace rtc
