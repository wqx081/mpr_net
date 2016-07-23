#ifndef COMMON_SUBSCRIPTION_H_
#define COMMON_SUBSCRIPTION_H_
#include "base/macros.h"
#include "common/types.h"
#include "common/observable.h"


namespace fb {

template<typename T>
class Subscription {
 public:
  Subscription() {}
  Subscription(const Subscription&) = delete;
  Subscription(Subscription&& other) noexcept {
    *this = std::move(other);
  }
  Subscription& operator=(Subscription&& other) noexcept {
    Unsubscribe();
    unsubscriber_ = std::move(other.unsubscriber_);
    id_ = other.id_;
    other.unsubscriber_ = nullptr;
    other.id_ = 0;
    return *this;
  }
  ~Subscription() {
    Unsubscribe();
  }

 private:
  typedef typename Observable<T>::Unsubscriber Unsubscriber;

  Subscription(std::shared_ptr<Unsubscriber> unsubscriber, uint64_t id)
    : unsubscriber_(std::move(unsubscriber)), id_(id) {
  }

  void Unsubscribe() {
    if (unsubscriber_ && id_ > 0) {
      unsubscriber_->Unsubscribe(id_);
      id_ = 0;
      unsubscriber_ = nullptr;
    }
  }

  std::shared_ptr<Unsubscriber> unsubscriber_;
  uint64_t id_{0};

  friend class Observable<T>;
};

} // namespace fb
#endif // COMMON_SUBSCRIPTION_H_
