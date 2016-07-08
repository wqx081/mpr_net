// Example usage:
//   void Init();
//   MPR_DECLARE_ONCE(once_init);
//
//   // Calls Init() exactly once.
//   void InitOnce() {
//     CallOnce(&once_init, &Init);
//   }
//
#ifndef BASE_ONCE_H_
#define BASE_ONCE_H_

#include <stddef.h>
#include "base/atomicops.h"

namespace base {

typedef base::subtle::AtomicWord OnceType;

#define MPR_ONCE_INIT 0
#define MPR_DECLARE_ONCE(NAME) ::base::OnceType NAME

enum {
  ONCE_STATE_UNINITIALIZED = 0,
  ONCE_STATE_EXECUTING_FUNCTION = 1,
  ONCE_STATE_DONE = 2
};

typedef void (*NoArgFunction)();
typedef void (*PointerArgFunction)(void* arg);

template<typename T>
struct OneArgFunction {
  typedef void (*type)(T);
};

void CallOnceImpl(OnceType* once, PointerArgFunction init_func, void* arg);

inline void CallOne(OnceType* once, NoArgFunction init_func) {
  if (base::subtle::Acquire_Load(once) != ONCE_STATE_DONE) {
    CallOnceImpl(once, reinterpret_cast<PointerArgFunction>(init_func), nullptr);
  }
}

template<typename Arg>
inline void CallOnce(OnceType* once,
                     typename OneArgFunction<Arg*>::type init_func, Arg* arg) {
  if (subtle::Acquire_Load(once) != ONCE_STATE_DONE) {
    CallOnceImpl(once, reinterpret_cast<PointerArgFunction>(init_func),
                 static_cast<void*>(arg));
  }
}

} // namespace base
#endif // BASE_ONCE_H_
