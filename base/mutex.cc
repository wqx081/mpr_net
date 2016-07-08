#include "base/mutex.h"

#include <errno.h>

namespace base {

namespace {

inline void InitializeNativeHandle(pthread_mutex_t* mutex) {
  int result;
  result = pthread_mutex_init(mutex, nullptr);
  DCHECK_EQ(0, result);
  USE(result);
}

inline void InitializeRecursiveNativeHandle(pthread_mutex_t* mutex) {
  pthread_mutexattr_t attr;
  int result = pthread_mutexattr_init(&attr);
  DCHECK_EQ(0, result);
  result = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
  DCHECK_EQ(0, result);
  result = pthread_mutex_init(mutex, &attr);
  DCHECK_EQ(0, result);
  result = pthread_mutexattr_destroy(&attr);
  DCHECK_EQ(0, result);
  USE(result);
}

inline void  DestroyNativeHandle(pthread_mutex_t* mutex) {
  int result = pthread_mutex_destroy(mutex);
  DCHECK_EQ(0, result);
  USE(result);
}


inline void LockNativeHandle(pthread_mutex_t* mutex) {
  int result = pthread_mutex_lock(mutex);
  DCHECK_EQ(0, result);
  USE(result);
}


inline void UnlockNativeHandle(pthread_mutex_t* mutex) {
  int result = pthread_mutex_unlock(mutex);
  DCHECK_EQ(0, result);
  USE(result);
}


inline bool TryLockNativeHandle(pthread_mutex_t* mutex) {
  int result = pthread_mutex_trylock(mutex);
  if (result == EBUSY) {
    return false;
  }
  DCHECK_EQ(0, result);
  return true;
}

} // namespace

Mutex::Mutex() {
  InitializeNativeHandle(&native_handle_);
}

Mutex::~Mutex() {
  DestroyNativeHandle(&native_handle_);
}

void Mutex::Lock() {
  LockNativeHandle(&native_handle_);
}


void Mutex::Unlock() {
  UnlockNativeHandle(&native_handle_);
}

bool Mutex::TryLock() {
  if (!TryLockNativeHandle(&native_handle_)) {
    return false;
  }
  return true;
}

/////////////////////// Recursive Mutex
RecursiveMutex::RecursiveMutex() {
  InitializeRecursiveNativeHandle(&native_handle_);
}


RecursiveMutex::~RecursiveMutex() {
  DestroyNativeHandle(&native_handle_);
}


void RecursiveMutex::Lock() {
  LockNativeHandle(&native_handle_);
}


void RecursiveMutex::Unlock() {
  UnlockNativeHandle(&native_handle_);
}


bool RecursiveMutex::TryLock() {
  if (!TryLockNativeHandle(&native_handle_)) {
    return false;
  }
  return true;
}

///////////////////////// ReadWriteLock
ReadWriteLock::ReadWriteLock() : native_handle_(PTHREAD_RWLOCK_INITIALIZER) {}

ReadWriteLock::~ReadWriteLock() {
  int result = pthread_rwlock_destroy(&native_handle_);
  DCHECK_EQ(result, 0);
}

void ReadWriteLock::ReadAcquire() {
  int result = pthread_rwlock_rdlock(&native_handle_);
  DCHECK_EQ(result, 0) << ". " << strerror(result);
}

void ReadWriteLock::ReadRelease() {
  int result = pthread_rwlock_unlock(&native_handle_);
  DCHECK_EQ(result, 0) << ". " << strerror(result);
}

void ReadWriteLock::WriteAcquire() {
  int result = pthread_rwlock_wrlock(&native_handle_);
  DCHECK_EQ(result, 0) << ". " << strerror(result);
}

void ReadWriteLock::WriteRelease() {
  int result = pthread_rwlock_unlock(&native_handle_);
  DCHECK_EQ(result, 0) << ". " << strerror(result);
}

} // namespace base
