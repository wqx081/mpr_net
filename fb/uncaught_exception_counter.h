#ifndef FB_UNCAUGHT_EXCEPTION_COUNTER_H_
#define FB_UNCAUGHT_EXCEPTION_COUNTER_H_

#include <exception>

namespace __cxxabiv1 {
struct __cxa_eh_globals;
extern "C" __cxa_eh_globals* __cxa_get_globals() noexcept;
}


namespace fb { namespace detail {

class UncaughtExceptionCounter {
 public:
  UncaughtExceptionCounter() : exception_count_(GetUncaughtExceptionCount()) {}
  UncaughtExceptionCounter(const UncaughtExceptionCounter& other)
      : exception_count_(other.exception_count_) {}
  bool IsNewUncaughtException() noexcept {
    return GetUncaughtExceptionCount() > exception_count_;
  }

 private:
  int GetUncaughtExceptionCount() noexcept;
  int exception_count_;
};

inline int UncaughtExceptionCounter::GetUncaughtExceptionCount() noexcept {
  return *(reinterpret_cast<unsigned int*>(static_cast<char*>(
      static_cast<void*>(__cxxabiv1::__cxa_get_globals())) + sizeof(void*)));
}


} // namespace detail
} // namespace fb

#endif //FB_UNCAUGHT_EXCEPTION_COUNTER_H_
