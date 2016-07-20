#include "fb/hhwheel_timer.h"
#include "fb/request.h"
#include "fb/scope_guard.h"

#include <cassert>

using std::chrono::milliseconds;

namespace fb {

int HHWheelTimer::DEFAULT_TICK_INTERVAL = 10;

HHWheelTimer::Callback::~Callback() {
  if (IsScheduled()) {
    CancelTimeout();
  }
}

void HHWheelTimer::Callback::SetScheduled(HHWheelTimer* wheel,
                                          milliseconds timeout) {
  assert(wheel_ == nullptr);
  assert(expiration_ == milliseconds(0));

  wheel_ = wheel;

  if (wheel_->count_ == 0 && !wheel_->processing_callbacks_guard_) {
    wheel_->now_ = GetCurrentTime();
  }

  expiration_ = wheel_->now_ + timeout;
}

void HHWheelTimer::Callback::CancelTimeoutImpl() {
  if (--wheel_->count_ <= 0) {
    assert(wheel_->count_ == 0);
    wheel_->AsyncTimeout::CancelTimeout();
  }
  hook_.unlink();

  wheel_ = nullptr;
  expiration_ = milliseconds(0);
}

HHWheelTimer::HHWheelTimer(EventBase* event_base,
                           milliseconds interval_ms) 
  : AsyncTimeout(event_base),
    interval_(interval_ms),
    next_tick_(1),
    count_(0),
    catchup_every_n_(DEFAULT_CATCHUP_EVERY_N),
    expirations_since_catchup_(0),
    processing_callbacks_guard_(false) {
}

HHWheelTimer::~HHWheelTimer() {
}

void HHWheelTimer::Destroy() {
  assert(count_ == 0);
  DelayedDestruction::Destroy();
}

void HHWheelTimer::ScheduleTimeoutImpl(Callback* callback,
                                       std::chrono::milliseconds timeout) {
  int64_t due = TimeToWheelTicks(timeout) + next_tick_;
  int64_t diff = due - next_tick_;
  CallbackList* list;

  if (diff < 0) {
    list = &buckets_[0][next_tick_ & WHEEL_MASK];
  } else if (diff < WHEEL_SIZE) {
    list = &buckets_[0][due & WHEEL_MASK];
  } else if (diff < 1 << (2 * WHEEL_BITS)) {
    list = &buckets_[1][(due >> WHEEL_BITS) & WHEEL_MASK];
  } else if (diff < 1 << (3 * WHEEL_BITS)) {
    list = &buckets_[2][(due >> 2 * WHEEL_BITS) & WHEEL_MASK];
  } else {
    if (diff > LARGEST_SLOT) {
      diff = LARGEST_SLOT;
      due = diff + next_tick_;
    }
    list = &buckets_[3][(due >> 3 * WHEEL_BITS) & WHEEL_MASK];
  }
  list->push_back(*callback);
}

void HHWheelTimer::ScheduleTimeout(Callback* callback,
                                   std::chrono::milliseconds timeout) {
  callback->CancelTimeout();

  callback->context_ = RequestContext::SaveContext();

  if (count_ == 0 && !processing_callbacks_guard_) {
    this->AsyncTimeout::ScheduleTimeout(interval_.count());
  }

  callback->SetScheduled(this, timeout);
  ScheduleTimeoutImpl(callback, timeout);
  count_++;
}

bool HHWheelTimer::CascadeTimers(int bucket, int tick) {
  CallbackList cbs;
  cbs.swap(buckets_[bucket][tick]);
  while (!cbs.empty()) {
    auto* cb = &cbs.front();
    cbs.pop_front();
    ScheduleTimeoutImpl(cb, cb->GetTimeRemaining(now_));
  }

  return tick == 0;
}

void HHWheelTimer::TimeoutExpired() noexcept {
  DestructorGuard dg(this);
  processing_callbacks_guard_ = true;
  auto reEntryGuard = MakeGuard([&] {
    processing_callbacks_guard_ = false;
  });
          
  milliseconds catchup = now_ + interval_;

  if (++expirations_since_catchup_ >= catchup_every_n_) {
    catchup = std::chrono::duration_cast<milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch());
    expirations_since_catchup_= 0;
  }
  while (now_ < catchup) {
    now_ += interval_;

    int idx = next_tick_ & WHEEL_MASK;
    if (0 == idx) {
      if (CascadeTimers(1, (next_tick_ >> WHEEL_BITS) & WHEEL_MASK) &&
          CascadeTimers(2, (next_tick_ >> (2 * WHEEL_BITS)) & WHEEL_MASK)) {
        CascadeTimers(3, (next_tick_ >> (3 * WHEEL_BITS)) & WHEEL_MASK);
      }
    }

    next_tick_++;
    CallbackList* cbs = &buckets_[0][idx];
    while (!cbs->empty()) {
      auto* cb = &cbs->front();
      cbs->pop_front();
      count_--;
      cb->wheel_ = nullptr;
      cb->expiration_ = milliseconds(0);
      auto old_ctx = RequestContext::SetContext(cb->context_);
      cb->TimeoutExpired();
      RequestContext::SetContext(old_ctx);
    }
  }
  if (count_ > 0) {
    this->AsyncTimeout::ScheduleTimeout(interval_.count());
  }
}

size_t HHWheelTimer::CancelAll() {
  decltype(buckets_) buckets;

  std::swap(buckets, buckets_);

  size_t count = 0;

  for (auto& tick : buckets) {
    for (auto& bucket : tick) {
      while (!bucket.empty()) {
        auto& cb = bucket.front();
        cb.CancelTimeout();
        cb.CallbackCanceled();
        count++;
      }
    }
  }

  return count;
}

} // namespace fb
