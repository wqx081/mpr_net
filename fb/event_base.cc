#include "fb/event_base.h"
#include "fb/notification_queue.h"
#include <condition_variable>
#include <fcntl.h>
#include <mutex>
#include <pthread.h>
#include <unistd.h>

namespace {

using fb::Cob;
using fb::EventBase;

template<typename Callback>
class FunctionLoopCallback : public EventBase::LoopCallback {
 public:
  explicit FunctionLoopCallback(Cob&& function)
      : function_(std::move(function)) {
  }
  explicit FunctionLoopCallback(const Cob& function)
      : function_(function) {
  }

  virtual void RunLoopCallback() noexcept {
    function_();
    delete this;
  }

 private:
  Callback function_;
};

} // namespace

namespace fb {

const int kNoFD = -1;

class EventBase::FunctionRunner : public NotificationQueue<
				            std::pair<void(*)(void*), 
					    void*>>::Consumer {
 public:
  void MessageAvailable(std::pair<void(*)(void*), void*>&& msg) {

    event_base_loopbreak(GetEventBase()->evb_);

    if (msg.first == nullptr && msg.second == nullptr) {
      return;
    }

    if (!msg.first) {
      LOG(ERROR) << "--------";
      return;
    }

    try {
      msg.first(msg.second);
    } catch (const std::exception& ex) {
      abort();
    } catch (...) {
      abort();
    }
  }
};

void EventBase::CobTimeout::TimeoutExpired() noexcept {
  try {
    cob_();
  } catch (const std::exception& ex) {
  
  } catch (...) {
  
  }
  delete this;
}

static std::mutex libevent_mutex_;

EventBase::EventBase(bool enable_time_measurement)
    : run_once_callbacks_(nullptr),
      stop_(false),
      loop_thread_(0),
      queue_(nullptr),
      fn_runner_(nullptr),
      max_latency_(0),
      avg_loop_time_(2000000),
      max_latency_loop_time_(avg_loop_time_),
      enable_time_measurement_(enable_time_measurement),
      next_loop_cnt_(-40),
      latest_loop_cnt_(next_loop_cnt_),
      start_work_(0),
      observer_(nullptr),
      observer_sample_count_(0) {
  {
    std::lock_guard<std::mutex> lock(libevent_mutex_);
    struct event ev;
    event_set(&ev, 0, 0, nullptr, nullptr);
    evb_ = (ev.ev_base) ? event_base_new() : event_init();
  }
  if (MPR_UNLIKELY(evb_ == nullptr)) {
    LOG(ERROR) << "....";
    ;//throw
  }
  VLOG(5) << "EventBase(): Created";

  InitNotificationQueue();
  RequestContext::GetStaticContext();
}

EventBase::EventBase(event_base* evb, bool enable_time_measurement)
    : run_once_callbacks_(nullptr),
      stop_(false),
      loop_thread_(0),
      evb_(evb),
      queue_(nullptr),
      fn_runner_(nullptr),
      max_latency_(0),
      avg_loop_time_(2000000),
      max_latency_loop_time_(avg_loop_time_),
      enable_time_measurement_(enable_time_measurement),
      next_loop_cnt_(-40),
      latest_loop_cnt_(next_loop_cnt_),
      start_work_(0),
      observer_(nullptr),
      observer_sample_count_(0) {
  if (MPR_UNLIKELY(evb_ == nullptr)) {
    LOG(ERROR) << "xxxx";
    //throw
  }

  InitNotificationQueue();
  RequestContext::GetStaticContext();
}

EventBase::~EventBase() {
  while (!on_destruction_callbacks_.empty()) {
    LoopCallback* callback = &on_destruction_callbacks_.front();
    on_destruction_callbacks_.pop_front();
    callback->RunLoopCallback();  
  }

  while (!pending_cob_timeouts_.empty()) {
    CobTimeout* timeout = &pending_cob_timeouts_.front();
    delete timeout;
  }

  while (!run_before_loop_callbacks_.empty()) {
    delete &run_before_loop_callbacks_.front();
  }

  (void) RunLoopCallbacks(false);

  if (!fn_runner_->ConsumeUntilDrained()) {
    LOG(ERROR) << ".....";
  }

  fn_runner_->StopConsuming();
  {
    std::lock_guard<std::mutex> lock(libevent_mutex_);
    event_base_free(evb_);
  }
  VLOG(5) << "EventBase(): Destroyed";
}

int EventBase::GetNotificationQueueSize() const {
  return queue_->Size();
}

void EventBase::SetMaxReadAtOnce(uint32_t max_at_once) {
  fn_runner_->SetMaxReadAtOnce(max_at_once);
}

void EventBase::SetLoadAvgMsec(uint32_t ms) {
  assert(enable_time_measurement_);
  uint64_t us = 1000 * ms;
  if (ms > 0) {
    max_latency_loop_time_.SetTimeInterval(us);
    avg_loop_time_.SetTimeInterval(us);
  } else {
    //ERROR
  }
}

void EventBase::ResetLoadAvg(double value) {
  assert(enable_time_measurement_);
  avg_loop_time_.Reset(value);
  max_latency_loop_time_.Reset(value);
}

static std::chrono::milliseconds
GetTimeDelta(std::chrono::steady_clock::time_point* prev) {
  auto result = std::chrono::steady_clock::now() - *prev;
  *prev = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(result);
}

void EventBase::WaitUntilRunning() {
  while (!IsRunning()) {
    sched_yield();
  }
}

bool EventBase::Loop() {
  return LoopBody();
}

bool EventBase::LoopOnce(int flags) {
  return LoopBody(flags | EVLOOP_ONCE);
}

bool EventBase::LoopBody(int flags) {
  VLOG(5) << "EventBase(): Starting loop.";
  int res = 0;
  bool ran_loop_callbacks;
  bool blocking = !(flags & EVLOOP_NONBLOCK);
  bool once = (flags & EVLOOP_ONCE);

  std::chrono::steady_clock::time_point prev;
  int64_t idle_start;
  int64_t busy;
  int64_t idle;

  loop_thread_.store(pthread_self(), std::memory_order_release);

  if (!name_.empty()) {
    // SetThreadName(name_); //TODO
  }

  if (enable_time_measurement_) {
    prev = std::chrono::steady_clock::now();
    idle_start = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  }

  while (!stop_) {
    ++next_loop_cnt_;

    LoopCallbackList callbacks;
    callbacks.swap(run_before_loop_callbacks_);

    while (!callbacks.empty()) {
      auto* item = &callbacks.front();
      callbacks.pop_front();
      item->RunLoopCallback();
    }

    if (blocking && loop_callbacks_.empty()) {
      res = event_base_loop(evb_, EVLOOP_ONCE);
    } else {
      res = event_base_loop(evb_, EVLOOP_ONCE | EVLOOP_NONBLOCK);
    }

    ran_loop_callbacks = RunLoopCallbacks();

    if (enable_time_measurement_) {
      
    }
  }

  return true;
}

void EventBase::LoopForever() {
  fn_runner_->StopConsuming();
  fn_runner_->StartConsuming(this, queue_.get());

  bool ret = Loop();

  fn_runner_->StopConsuming();
  fn_runner_->StartConsumingInternal(this, queue_.get());

  if (!ret) {
  }
}

bool EventBase::BumpHandlingTime() {
  if (NothingHandledYet()) {
    latest_loop_cnt_ = next_loop_cnt_;
    start_work_ = std::chrono::duration_cast<std::chrono::microseconds>(
		    std::chrono::steady_clock::now().time_since_epoch()).count();
    return true;
  }
  return false;
}

void EventBase::TerminateLoopSoon() {
  stop_ = true;
  event_base_loopbreak(evb_);
  try {
    queue_->PutMessage(std::make_pair(nullptr, nullptr));
  } catch (...) {
  }
}

void EventBase::RunInLoop(LoopCallback* callback,
		          bool this_iteration) {
  DCHECK(IsInEventBaseThread());
  callback->CancelLoopCallback();
  callback->context_ = RequestContext::SaveContext();
  if (run_once_callbacks_ != nullptr && this_iteration) {
    run_once_callbacks_->push_back(*callback);
  } else {
    loop_callbacks_.push_back(*callback);
  }
}

void EventBase::RunInLoop(const Cob& cob, bool this_iteration) {
  DCHECK(IsInEventBaseThread());
  auto wrapper = new FunctionLoopCallback<Cob>(cob);
  wrapper->context_ = RequestContext::SaveContext();
  if (run_once_callbacks_ != nullptr && this_iteration) {
    run_once_callbacks_->push_back(*wrapper);
  } else {
    loop_callbacks_.push_back(*wrapper);
  }
}


} // namespace fb
