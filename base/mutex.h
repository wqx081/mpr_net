#ifndef BASE_MUTEX_H_
#define BASE_MUTEX_H_
#include "base/lazy_instance.h"
#include <pthread.h>

namespace base {

class ConditionVariable;

class Mutex final {
 public:
  typedef pthread_mutex_t NativeHandle;
  Mutex();
  ~Mutex();

  void Lock();
  void Unlock();
  bool TryLock();
  NativeHandle& native_handle() {
    return native_handle_;
  }
  const NativeHandle& native_handle() const {
    return native_handle_;
  }

 private:
  NativeHandle native_handle_;
  friend class ConditionVariable;
  
  DISALLOW_COPY_AND_ASSIGN(Mutex);
};

// POD Mutex initialized laizly
// Usage:
//   static LazyMutex my_mutex = LAZY_MUTEX_INITIALIZER;
//
//   void my_function() {
//     LockGuard<Mutex> guard(my_mutex.Pointer());
//     // Do something.
//   }
typedef LazyStaticInstance<Mutex,
                           DefaultConstructTrait<Mutex>,
                           ThreadSafeInitOnceTrait>::type LazyMutex;


class RecursiveMutex final {
 public:
  typedef Mutex::NativeHandle NativeHandle;
  RecursiveMutex();
  ~RecursiveMutex();
  
  void Lock();
  void Unlock();
  bool TryLock();
  NativeHandle& native_handle() {
    return native_handle_;
  }
  const NativeHandle& native_handle() const {
    return native_handle_;
  }

 private:
  NativeHandle native_handle_;

  DISALLOW_COPY_AND_ASSIGN(RecursiveMutex);
};


typedef LazyStaticInstance<RecursiveMutex,
                           DefaultConstructTrait<RecursiveMutex>,
                           ThreadSafeInitOnceTrait>::type LazyRecursiveMutex;

#define LAZY_RECURSIVE_MUTEX_INITIALIZER LAZY_STATIC_INSTANCE_INITIALIZER

template <typename Mutex>
class LockGuard final {
 public:
  explicit LockGuard(Mutex* mutex) : mutex_(mutex) { mutex_->Lock(); }
  ~LockGuard() { mutex_->Unlock(); }

 private:
  Mutex* mutex_;

  DISALLOW_COPY_AND_ASSIGN(LockGuard);
};


class ReadWriteLock {
 public:
  ReadWriteLock();
  ~ReadWriteLock();

  void ReadAcquire();
  void ReadRelease();

  void WriteAcquire();
  void WriteRelease();

 private:
  using NativeHandle = pthread_rwlock_t;
  NativeHandle native_handle_;
  
  DISALLOW_COPY_AND_ASSIGN(ReadWriteLock);
};

class AutoReadLock {
 public:
  explicit AutoReadLock(ReadWriteLock& lock) : lock_(lock) {
    lock_.ReadAcquire();
  }
  ~AutoReadLock() {
    lock_.ReadRelease();
  }

 private:
  ReadWriteLock& lock_;
 
  DISALLOW_COPY_AND_ASSIGN(AutoReadLock);
};

class AutoWriteLock {
 public:
  explicit AutoWriteLock(ReadWriteLock& lock) : lock_(lock) {
    lock_.WriteAcquire();
  }
  ~AutoWriteLock() {
    lock_.WriteRelease();
  }

 private:
  ReadWriteLock& lock_;
  DISALLOW_COPY_AND_ASSIGN(AutoWriteLock);
};

} // namespace base
#endif // BASE_MUTEX_H_
