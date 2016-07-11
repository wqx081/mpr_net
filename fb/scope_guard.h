#ifndef FB_SCOPEGUARD_H_
#define FB_SCOPEGUARD_H_

#include <cstddef>
#include <functional>
#include <new>

#include "fb/uncaught_exception_counter.h"

namespace fb {

/**
 * ScopeGuard is a general implementation of the "Initialization is
 * Resource Acquisition" idiom.  Basically, it guarantees that a function
 * is executed upon leaving the currrent scope unless otherwise told.
 *
 * The MakeGuard() function is used to create a new ScopeGuard object.
 * It can be instantiated with a lambda function, a std::function<void()>,
 * a functor, or a void(*)() function pointer.
 *
 *
 * Usage example: Add a friend to memory iff it is also added to the db.
 *
 * void User::addFriend(User& newFriend) {
 *   // add the friend to memory
 *   friends_.push_back(&newFriend);
 *
 *   // If the db insertion that follows fails, we should
 *   // remove it from memory.
 *   // (You could also declare this as "auto guard = MakeGuard(...)")
 *   ScopeGuard guard = MakeGuard([&] { friends_.pop_back(); });
 *
 *   // this will throw an exception upon error, which
 *   // makes the ScopeGuard execute UserCont::pop_back()
 *   // once the Guard's destructor is called.
 *   db_->addFriend(GetName(), newFriend.GetName());
 *
 *   // an exception was not thrown, so don't execute
 *   // the Guard.
 *   guard.dismiss();
 * }
 *
 * Examine ScopeGuardTest.cpp for some more sample usage.
 *
 * Stolen from:
 *   Andrei's and Petru Marginean's CUJ article:
 *     http://drdobbs.com/184403758
 *   and the loki library:
 *     http://loki-lib.sourceforge.net/index.php?n=Idioms.ScopeGuardPointer
 *   and triendl.kj article:
 *     http://www.codeproject.com/KB/cpp/scope_guard.aspx
 */
class ScopeGuardImplBase {
 public:
  void dismiss() noexcept {
    dismissed_ = true;
  }

 protected:
  ScopeGuardImplBase()
    : dismissed_(false) {}

  ScopeGuardImplBase(ScopeGuardImplBase&& other) noexcept
    : dismissed_(other.dismissed_) {
    other.dismissed_ = true;
  }

  bool dismissed_;
};

template <typename FunctionType>
class ScopeGuardImpl : public ScopeGuardImplBase {
 public:
  explicit ScopeGuardImpl(const FunctionType& fn)
    : function_(fn) {}

  explicit ScopeGuardImpl(FunctionType&& fn)
    : function_(std::move(fn)) {}

  ScopeGuardImpl(ScopeGuardImpl&& other)
    : ScopeGuardImplBase(std::move(other))
    , function_(std::move(other.function_)) {
  }

  ~ScopeGuardImpl() noexcept {
    if (!dismissed_) {
      execute();
    }
  }

 private:
  void* operator new(size_t) = delete;

  void execute() noexcept { function_(); }

  FunctionType function_;
};

template <typename FunctionType>
ScopeGuardImpl<typename std::decay<FunctionType>::type>
MakeGuard(FunctionType&& fn) {
  return ScopeGuardImpl<typename std::decay<FunctionType>::type>(
      std::forward<FunctionType>(fn));
}

/**
 * This is largely unneeded if you just use auto for your guards.
 */
typedef ScopeGuardImplBase&& ScopeGuard;

namespace detail {

/**
 * ScopeGuard used for executing a function when leaving the current scope
 * depending on the presence of a new uncaught exception.
 *
 * If the executeOnException template parameter is true, the function is
 * executed if a new uncaught exception is present at the end of the scope.
 * If the parameter is false, then the function is executed if no new uncaught
 * exceptions are present at the end of the scope.
 *
 * Used to implement SCOPE_FAIL and SCOPE_SUCCES below.
 */
template <typename FunctionType, bool executeOnException>
class ScopeGuardForNewException {
 public:
  explicit ScopeGuardForNewException(const FunctionType& fn)
      : function_(fn) {
  }

  explicit ScopeGuardForNewException(FunctionType&& fn)
      : function_(std::move(fn)) {
  }

  ScopeGuardForNewException(ScopeGuardForNewException&& other)
      : function_(std::move(other.function_))
      , exceptionCounter_(std::move(other.exceptionCounter_)) {
  }

  ~ScopeGuardForNewException() noexcept(executeOnException) {
    if (executeOnException == exceptionCounter_.IsNewUncaughtException()) {
      function_();
    }
  }

 private:
  ScopeGuardForNewException(const ScopeGuardForNewException& other) = delete;

  void* operator new(size_t) = delete;

  FunctionType function_;
  UncaughtExceptionCounter exceptionCounter_;
};

/**
 * Internal use for the macro SCOPE_FAIL below
 */
enum class ScopeGuardOnFail {};

template <typename FunctionType>
ScopeGuardForNewException<typename std::decay<FunctionType>::type, true>
operator+(detail::ScopeGuardOnFail, FunctionType&& fn) {
  return
      ScopeGuardForNewException<typename std::decay<FunctionType>::type, true>(
      std::forward<FunctionType>(fn));
}

/**
 * Internal use for the macro SCOPE_SUCCESS below
 */
enum class ScopeGuardOnSuccess {};

template <typename FunctionType>
ScopeGuardForNewException<typename std::decay<FunctionType>::type, false>
operator+(ScopeGuardOnSuccess, FunctionType&& fn) {
  return
      ScopeGuardForNewException<typename std::decay<FunctionType>::type, false>(
      std::forward<FunctionType>(fn));
}

/**
 * Internal use for the macro SCOPE_EXIT below
 */
enum class ScopeGuardOnExit {};

template <typename FunctionType>
ScopeGuardImpl<typename std::decay<FunctionType>::type>
operator+(detail::ScopeGuardOnExit, FunctionType&& fn) {
  return ScopeGuardImpl<typename std::decay<FunctionType>::type>(
      std::forward<FunctionType>(fn));
}
} // namespace detail

} // fb

#ifndef FB_ANONYMOUS_VARIABLE
#define FB_CONCATENATE_IMPL(s1, s2) s1##s2
#define FB_CONCATENATE(s1, s2) FB_CONCATENATE_IMPL(s1, s2)
#ifdef __COUNTER__
#define FB_ANONYMOUS_VARIABLE(str) FB_CONCATENATE(str, __COUNTER__)
#else
#define FB_ANONYMOUS_VARIABLE(str) FB_CONCATENATE(str, __LINE__)
#endif
#endif

#define SCOPE_EXIT \
  auto FB_ANONYMOUS_VARIABLE(SCOPE_EXIT_STATE) \
  = ::fb::detail::ScopeGuardOnExit() + [&]() noexcept

#define SCOPE_FAIL \
  auto FB_ANONYMOUS_VARIABLE(SCOPE_FAIL_STATE) \
  = ::fb::detail::ScopeGuardOnFail() + [&]() noexcept

#define SCOPE_SUCCESS \
  auto FB_ANONYMOUS_VARIABLE(SCOPE_SUCCESS_STATE) \
  = ::fb::detail::ScopeGuardOnSuccess() + [&]()

#endif // FB_SCOPEGUARD_H_
