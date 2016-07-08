#include "base/once.h"
#include <sched.h>
#include "base/atomicops.h"

namespace base {

void CallOnceImpl(OnceType* once, PointerArgFunction init_func, void* arg) {
  subtle::AtomicWord state = subtle::Acquire_Load(once);
  if (state == ONCE_STATE_DONE) {
    return;
  }  

  state = subtle::Acquire_CompareAndSwap(once,
                                         ONCE_STATE_UNINITIALIZED,
                                         ONCE_STATE_EXECUTING_FUNCTION);
  if (state == ONCE_STATE_UNINITIALIZED) {
    init_func(arg);
    subtle::Release_Store(once, ONCE_STATE_DONE);
  } else {
    while (state == ONCE_STATE_EXECUTING_FUNCTION) {
      sched_yield();
      state = subtle::Acquire_Load(once);
    }
  }
}

} // namespace base
