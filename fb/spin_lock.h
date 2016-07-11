#ifndef FB_SPIN_LOCK_H_
#define FB_SPIN_LOCK_H_
#include "fb/spin_lock_imp.h"

namespace fb {

typedef SpinLockMslImpl SpinLock;

template<typename LOCK>
class SpinLockGuardImpl {
 public:
  explicit SpinLockGuardImpl(LOCK& lock) : lock_(lock) {
    lock_.lock();
  }
  ~SpinLockGuardImpl() {
    lock_.unlock();
  }

 private:
  LOCK& lock_;

  DISALLOW_COPY_AND_ASSIGN(SpinLockGuardImpl);
};

} // namespace fb
#endif // FB_SPIN_LOCK_H_
