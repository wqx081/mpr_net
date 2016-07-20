#ifndef FB_HHWHEEL_TIMER_H_
#define FB_HHWHEEL_TIMER_H_
#include "fb/async_timeout.h"
#include "fb/delayed_destruction.h"

#include <boost/intrusive/list.hpp>
#include <glog/logging.h>

#include <chrono>
#include <cstddef>
#include <memory>
#include <list>

namespace fb {

class HHWheelTimer : 
  public AsyncTimeout,
  public DelayedDestruction {
 public:
  typedef std::unique_ptr<HHWheelTimer, Destructor> UniquePtr;

  class Callback {
   public:
    Callback() : wheel_(nullptr), expiration_(0) {}
    virtual ~Callback();

    virtual void TimeoutExpired() noexcept = 0;
    virtual void CallbackCanceled() noexcept {
      TimeoutExpired();
    }
    void CancelTimeout() {
      if (wheel_ == nullptr) {
        return;
      }
      CancelTimeoutImpl();
    }
    bool IsScheduled() const {
      return wheel_ != nullptr;
    }

   protected:
    virtual std::chrono::milliseconds GetCurrentTime() {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch());
    }

   private:
    HHWheelTimer* wheel_;
    std::chrono::milliseconds expiration_;
    typedef boost::intrusive::list_member_hook<
      boost::intrusive::link_mode<boost::intrusive::auto_unlink>> ListHook;
    ListHook hook_;
    typedef boost::intrusive::list<
      Callback,
      boost::intrusive::member_hook<Callback, ListHook, &Callback::hook_>,
      boost::intrusive::constant_time_size<false>> List;
    std::shared_ptr<RequestContext> context_;
    friend class HHWheelTimer;

    std::chrono::milliseconds GetTimeRemaining(std::chrono::milliseconds now) const {
      if (now >= expiration_) {
        return std::chrono::milliseconds(0);
      }
      return expiration_ - now;
    }
    void SetScheduled(HHWheelTimer* wheel,
                      std::chrono::milliseconds);
    void CancelTimeoutImpl();
  };  

  static int DEFAULT_TICK_INTERVAL;
  explicit HHWheelTimer(EventBase* event_base,
                        std::chrono::milliseconds interval_ms =
                        std::chrono::milliseconds(DEFAULT_TICK_INTERVAL));

  virtual void Destroy();
  size_t CancelAll();

  std::chrono::milliseconds GetTickInterval() const {
    return interval_;
  }

  void ScheduleTimeout(Callback* callback,
                       std::chrono::milliseconds timeout);
  void ScheduleTimeoutImpl(Callback* callback,
                           std::chrono::milliseconds timeout);
  template<typename F>
  void ScheduleTimeoutFn(F fn, std::chrono::milliseconds timeout) {
    struct Wrapper : Callback {
      Wrapper(F f) : fn_(std::move(f)) {}
      void TimeoutExpired() noexcept override {
        try {
          fn_();
        } catch (const std::exception& e) {
        } catch (...) {
        }
        delete this;
      }
      F fn_;
    };
    Wrapper* w = new Wrapper(std::move(fn));
    ScheduleTimeout(w, timeout);
  }

  uint64_t Count() const {
    return count_;
  }

  void SetCatchupEveryN(uint32_t every_n) {
    catchup_every_n_ = every_n;
  }

  bool IsDetachable() const {
    return !AsyncTimeout::IsScheduled();
  }

  using AsyncTimeout::AttachEventBase;
  using AsyncTimeout::DetachEventBase;
  using AsyncTimeout::GetTimeoutManager;

 protected:
  virtual ~HHWheelTimer();

 private:
  std::chrono::milliseconds interval_;

  static constexpr int WHEEL_BUCKETS = 4;
  static constexpr int WHEEL_BITS = 8;
  static constexpr unsigned int WHEEL_SIZE = (1 << WHEEL_BITS);
  static constexpr unsigned int WHEEL_MASK = (WHEEL_SIZE - 1);
  static constexpr uint32_t LARGEST_SLOT = 0xffffffffUL;

  typedef Callback::List CallbackList;
  CallbackList buckets_[WHEEL_BUCKETS][WHEEL_SIZE];

  int64_t TimeToWheelTicks(std::chrono::milliseconds t) {
    return t.count() / interval_.count();
  }

  bool CascadeTimers(int bucket, int tick);
  int64_t next_tick_;
  uint64_t count_;
  std::chrono::milliseconds now_;

  static constexpr uint32_t DEFAULT_CATCHUP_EVERY_N = 10;

  uint32_t catchup_every_n_;
  uint32_t expirations_since_catchup_;
  bool processing_callbacks_guard_;

  virtual void TimeoutExpired() noexcept;
  DISALLOW_COPY_AND_ASSIGN(HHWheelTimer);
};

} // namespace fb
#endif // FB_HHWHEEL_TIMER_H_
