#ifndef COMMON_OBSERVABLE_H_
#define COMMON_OBSERVABLE_H_
#include "base/macros.h"

#include "common/types.h"
#include "common/subject.h"
#include "common/subscription.h"

#include "fb/rw_spin_lock.h"
#include "fb/small_locks.h"
#include "fb/thread_local.h"
#include "fb/executor.h"
#include <map>
#include <vector>
#include <utility>
#include <memory>

namespace fb {

template<typename T, size_t InlineObservers>
struct Observable {
 public:
  Observable(): next_subscription_id_(1) {}
  Observable(Observable&& other) = delete;
  virtual ~Observable() {
  }

  virtual Subscription<T> Subscribe(ObserverPtr<T> observer) {
    return SubscribeImpl(observer, false);
  }  
  virtual void Observe(ObserverPtr<T> observer) {
    SubscribeImpl(observer, true);
  }
  virtual void Observe(Observer<T>* observer) {
    if (in_callback_ && * in_callback_) {
      if (!new_observers_) {
        new_observers_.reset(new ObserverList());
      }
      new_observers_->push_back(observer);
    } else {
      RWSpinLock::WriteHolder{&observers_lock_};
      observers_.push_back(observer);
    }
  }

  ObserverPtr<T> ObserveOn(SchedulerPtr scheduler) {
    struct ViaSubject : public Observable<T> {
      ViaSubject(SchedulerPtr sched,
                 Observable* obs)
          : scheduler_(sched), observable_(obs) {}

      Subscription<T> Subscribe(ObserverPtr<T> o) override {
        return observable_->Subscribe(
          Observer<T>::Create(
           [=](T val) { scheduler_->Add([o, val] { o->OnNext(val); }); },
           [=](Error e) { scheduler_->Add([o, e] { o->OnError(e);  }); },
           [=]() { scheduler_->Add([o] { o->OnCompleted();  }); }));
      }

      protected:
       SchedulerPtr scheduler_;
       Observable* observable_;
    };
    return std::make_shared<ViaSubject>(scheduler, this);
  }

  std::unique_ptr<Observable> SubscribeOn(SchedulerPtr scheduler) {
    struct Subject_ : public Subject<T> {
     public:
      Subject_(SchedulerPtr s, Observable* o)
          : scheduler_(s), observable_(o) {
      }
      Subscription<T> Subscribe(ObserverPtr<T> o) {
        scheduler_->Add([=] {
	  observable_->Subscribe(o);		
        });
	return Subscription<T>(nullptr, 0);
      }
     protected:
      SchedulerPtr scheduler_;
      Observable* observable_;
    };

    return make_unique<Subject_>(scheduler, this);
  }

 protected:
  template<typename F>
  void ForEachObserver(F f) {
    if (MPR_UNLIKELY(!in_callback_)) {
      in_callback_.reset(new bool{false});
    }
    CHECK(!(*in_callback_));
    *in_callback_ = true;

    {
      RWSpinLock::ReadHolder rh(observers_lock_);
      for (auto o : observers_) {
        f(o);
      }

      for (auto& kv : subscribers_) {
        f(kv.second.get());
      }
    }

    if (MPR_UNLIKELY((new_observers_ && !new_observers_->empty()) ||
                     (new_subscribers_ && !new_subscribers_->empty()) ||
		     (old_subscribers_ && !old_subscribers_->empty()))) {

      RWSpinLock::WriteHolder wh(observers_lock_);
      if (new_observers_) {
        for (auto observer : *(new_observers_)) {
	  observers_.push_back(observer);
	}
	new_observers_->clear();
      }
      if (new_subscribers_) {
        for (auto& kv : * (new_subscribers_)) {
	  subscribers_.insert(std::move(kv));
	}
	new_subscribers_->clear();
      }

      if (old_subscribers_) {
        for (auto& id : *(old_subscribers_)) {
	  subscribers_.erase(id);
	}
	old_subscribers_->clear();
      }
    }
    *in_callback_ = false;
  }

 private:

  Subscription<T> SubscribeImpl(ObserverPtr<T> observer,
		                bool indefinite) {
    auto subscription = MakeSubscription(indefinite);
    typename SubscriberMap::value_type kv{subscription.id_,
                                          std::move(observer)};
    if (in_callback_ && *in_callback_) {
      if (!new_subscribers_) {
        new_subscribers_.reset(new SubscriberMap());
      }
      new_subscribers_->insert(std::move(kv));
    } else {
      RWSpinLock::WriteHolder{&observers_lock_};
      subscribers_.insert(std::move(kv));
    }
    return subscription;
  }

  class Unsubscriber {
   public:
    explicit Unsubscriber(Observable* observable)
        : observable_(observable) {
      CHECK(observable_);
    }
   
    void Unsubscribe(uint64_t id) {
      CHECK(id > 0);
      RWSpinLock::ReadHolder guard(lock_);
      if (observable_) {
        observable_->Unsubscribe(id);
      }
    }

    void Disable() {
      RWSpinLock::WriteHolder guard(lock_);
      observable_ = nullptr;
    }

   private:
    RWSpinLock lock_;
    Observable* observable_;
  };

  std::shared_ptr<Unsubscriber> unsubscriber_{nullptr};
  MicroSpinLock unsubscriber_lock_{0};

  friend class Subscription<T>;

  void Unsubscribe(uint64_t id) {
    if (in_callback_ && *in_callback_) {
      if (!old_subscribers_) {
        old_subscribers_.reset(new std::vector<uint64_t>());
      }
      if (new_subscribers_) {
        auto it = new_subscribers_->find(id);
	if (it != new_subscribers_->end()) {
	  new_subscribers_->erase(it);
	  return;
	}
      }
      old_subscribers_->push_back(id);
    } else {
      RWSpinLock::WriteHolder{&observers_lock_};
      subscribers_.erase(id);
    }
  }

  Subscription<T> MakeSubscription(bool indefinite) {
    if (indefinite) {
      return Subscription<T>(nullptr, next_subscription_id_++);
    } else {
      if (!unsubscriber_) {
        std::lock_guard<MicroSpinLock> guard(unsubscriber_lock_);
	if (!unsubscriber_) {
	  unsubscriber_ = std::make_shared<Unsubscriber>(this);
	}
      }
      return Subscription<T>(unsubscriber_, next_subscription_id_++);
    }
  }

  std::atomic<uint64_t> next_subscription_id_;
  RWSpinLock observers_lock_;
  ThreadLocalPtr<bool> in_callback_;


  //std::vector<Observer<T>*> observer_list_;
  typedef std::vector<Observer<T>*> ObserverList;
  size_t inline_observers_size_;
  ObserverList observers_;
  ThreadLocalPtr<ObserverList> new_observers_;


  typedef std::map<uint64_t, ObserverPtr<T>> SubscriberMap;
  SubscriberMap subscribers_;  
  ThreadLocalPtr<SubscriberMap> new_subscribers_;
  ThreadLocalPtr<std::vector<uint64_t>> old_subscribers_;
};

} // namespace fb
#endif // COMMON_OBSERVABLE_H_
