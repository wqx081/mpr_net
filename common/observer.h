#ifndef COMMON_OBSERVER_H_
#define COMMON_OBSERVER_H_

#include "common/types.h"
#include "base/macros.h"
#include <functional>
#include <memory>
#include <stdexcept>

namespace fb {

template <typename T> struct FunctionObserver;

template<typename T>
struct Observer {
  virtual void OnNext(const T&) = 0;
  virtual void OnError(Error) = 0;
  virtual void OnCompleted() = 0;

  virtual ~Observer() {}

  template<typename N, typename E, typename C>
  static std::unique_ptr<Observer> Create(N&& on_next,
		                          E&& on_error,
					  C&& on_completed) {
    return make_unique<FunctionObserver<T>>(
      std::forward<N>(on_next),
      std::forward<E>(on_error),
      std::forward<C>(on_completed));
  }

  template<typename N, typename E>
  static std::unique_ptr<Observer> Create(N&& on_next,
		                          E&& on_error) {
    return make_unique<FunctionObserver<T>>(
      std::forward<N>(on_next),
      std::forward<E>(on_error),
      nullptr);
  }

  template<typename N>
  static std::unique_ptr<Observer> Create(N&& on_next) {
    return make_unique<FunctionObserver<T>>(
      std::forward<N>(on_next),
      nullptr,
      nullptr);
  }
};

template<typename T>
struct FunctionObserver : public Observer<T> {
  typedef std::function<void(const T&)> on_next_t;
  typedef std::function<void(Error)> on_error_t;
  typedef std::function<void()> on_completed_t;

  template<typename N = on_next_t,
	   typename E = on_error_t,
	   typename C = on_completed_t>
  FunctionObserver(N&& n, E&& e, C&& c)
    : on_next_(std::forward<N>(n)),
      on_error_(std::forward<E>(e)),
      on_completed_(std::forward<C>(c)) {}

  void OnNext(const T& val) override {
    if (on_next_) {
      on_next_(val);
    }
  }
  void OnError(Error e) override {
    if (on_error_) {
      on_error_(e);
    }
  }
  void OnCompleted() override {
    if (on_completed_) {
      on_completed_();
    }
  }

 protected:
  on_next_t on_next_;
  on_error_t on_error_;
  on_completed_t on_completed_; 
};

} // namespace fb
#endif // COMMON_OBSERVER_H_
