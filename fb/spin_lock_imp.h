#ifndef FB_SPIN_LOCK_IMPL_H_
#define FB_SPIN_LOCK_IMPL_H_
/*
* This class provides a few spin lock implementations, depending on the
* platform.  fb/SpinLock.h will select one of these as the fb::SpinLock
* implementation.
*
* The main reason we keep these separated out here is so that we can run unit
* tests for all supported spin lock implementations, even though only one will
* be selected as the actual fb::SpinLock implemenatation for any given
* platform.
*/

#include "base/macros.h"
#include "fb/small_locks.h"

namespace fb {

class SpinLockMslImpl {
 public:
  SpinLockMslImpl() {
    lock_.init();
  }
  void lock() const {
    lock_.lock();
  }
  void unlock() const {
    lock_.unlock();
  }
  bool trylock() const {
    return lock_.try_lock();
  } 
 private:
  mutable MicroSpinLock lock_;
};

class SpinLockPthreadImpl {
 public:
  SpinLockPthreadImpl() {
    pthread_spin_init(&lock_, PTHREAD_PROCESS_PRIVATE);
  }
  ~SpinLockPthreadImpl() {
    pthread_spin_destroy(&lock_);
  }
  void lock() const {
    pthread_spin_lock(&lock_);
  }
  void unlock() const {
    pthread_spin_unlock(&lock_);
  }
  bool trylock() const {
    int rc = pthread_spin_trylock(&lock_);
    if (rc == 0) {
      return true;
    } else if (rc == EBUSY) {
      return false;
    }
    //TODO
    return false;
  }

 private:
  mutable pthread_spinlock_t lock_;
};

class SpinLockPthreadMutexImpl {
 public:
  SpinLockPthreadMutexImpl() {
    pthread_mutex_init(&lock_, nullptr);
  }

  ~SpinLockPthreadMutexImpl() {
    pthread_mutex_destroy(&lock_);
  }

  void lock() const {
    pthread_mutex_lock(&lock_);
  }

  void unlock() const {
    pthread_mutex_unlock(&lock_);
  }

  bool trylock() const {
    int rc = pthread_mutex_trylock(&lock_);
    if (rc == 0) {
      return true;
    } else if (rc == EBUSY) {
      return false;
    }
    //TODO
    return false;
  }

 private:
  mutable pthread_mutex_t lock_;
};


} // namespace fb
#endif // FB_SPIN_LOCK_IMPL_H_
