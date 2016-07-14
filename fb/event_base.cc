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

class EventBase::FunctionRunner : public NotificationQueue<std::pair<void(*)(void*), void*>>::Consumer {
 public:
  void MessageAvailable(std::pair<void(*)(void*), void*>&& msg) {

    event_base_loopbreak(GetEventBase()->evb_);

    if (msg.first == nullptr && msg.second == nullptr) {
      return;
    }

    if (!msg.first) {
       LOG(ERROR) << "nullptr callback registered to be run in "
                   << "event base thread";
      return;
    }

    try {
      msg.first(msg.second);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "runInEventBaseThread() function threw a "
                   << typeid(ex).name() << " exception: " << ex.what();
      abort();
    } catch (...) {
      LOG(ERROR) << "runInEventBaseThread() function threw an exception";
      abort();
    }
  }
};

void EventBase::CobTimeout::TimeoutExpired() noexcept {
  try {
    cob_();
  } catch (const std::exception& ex) {
    LOG(ERROR) << "EventBase::runAfterDelay() callback threw "
               << typeid(ex).name() << " exception: " << ex.what();
  } catch (...) {
    LOG(ERROR) << "EventBase::runAfterDelay() callback threw non-exception "
               << "type";
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
    LOG(ERROR) << "EventBase(): Failed to init event base.";
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
    LOG(ERROR) << "EventBase(): Pass nullptr as event base.";
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
    LOG(ERROR) << "~EventBase(): Unable to drain notification queue";
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
    LOG(ERROR) << "non-positive arg to setLoadAvgMsec()";
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
      busy = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() - 
        start_work_;
      idle = start_work_ - idle_start;
    
      avg_loop_time_.AddSample(idle, busy);
      max_latency_loop_time_.AddSample(idle, busy);

      if (observer_) {
        if (observer_sample_count_++ == observer_->GetSampleRate()) {
          observer_sample_count_ = 0;
          observer_->LoopSample(busy, idle);
        }
      }

      //TODO
      //VLOG(11)

      if ((max_latency_ > 0) &&
          (max_latency_loop_time_.Get() > double(max_latency_))) {
        max_latency_cob_();
        max_latency_loop_time_.Dampen(0.9);
      }

      idle_start = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    } else {
        VLOG(11) << "EventBase "  << this << " did not timeout "
          " time measurement is disabled "
          " nothingHandledYet(): "<< NothingHandledYet();
    }

    if (res != 0 && !ran_loop_callbacks) {

      if (GetNotificationQueueSize() > 0) {
        fn_runner_->HandlerReady(0);
      } else {
        break;
      }
    }
    
    if (enable_time_measurement_) {
      VLOG(5) << "EventBase " << this << " loop time: " << GetTimeDelta(&prev).count();
    }

    if (once) {
      break; 
    }
  }

  stop_ = false;

  if (res < 0) {
    LOG(ERROR) << "EventBase: -- error in event loop, res = " << res;
    return false;
  } else if (res == 1) {
    VLOG(5) << "EventBase: ran out of events (exiting loop)!";
  } else if (res > 1) {
    LOG(ERROR) << "EventBase: unknown event loop result = " << res;
    return false;
  }

  loop_thread_.store(0, std::memory_order_release);

  VLOG(5) << "EventBase(): Done with loop.";
  return true;
}

void EventBase::LoopForever() {
  fn_runner_->StopConsuming();
  fn_runner_->StartConsuming(this, queue_.get());

  bool ret = Loop();

  fn_runner_->StopConsuming();
  fn_runner_->StartConsumingInternal(this, queue_.get());

  if (!ret) {
    LOG(ERROR) << "error in EventBase::LoopForever()";
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

void EventBase::RunInLoop(Cob&& cob, bool this_iteration) {
  DCHECK(IsInEventBaseThread());
  auto wrapper = new FunctionLoopCallback<Cob>(std::move(cob));
  wrapper->context_ = RequestContext::SaveContext();
  if (run_once_callbacks_ != nullptr && this_iteration) {
    run_once_callbacks_->push_back(*wrapper);
  } else {
    loop_callbacks_.push_back(*wrapper);
  }
}

void EventBase::RunOnDestruction(LoopCallback* callback) {
  DCHECK(IsInEventBaseThread());
  callback->CancelLoopCallback();
  on_destruction_callbacks_.push_back(*callback);
}
  
void EventBase::RunBeforeLoop(LoopCallback* callback) {
  DCHECK(IsInEventBaseThread());
  callback->CancelLoopCallback();
  run_before_loop_callbacks_.push_back(*callback);
}

bool EventBase::RunInEventBaseThread(void (*fn)(void*), void* arg) {
  if (!fn) {
    return false;
  }
  if (InRunningEventBaseThread()) {
    RunInLoop(new RunInLoopCallback(fn, arg));
    return true;
  }

  try {
    queue_->PutMessage(std::make_pair(fn, arg));
  } catch (const std::exception& ex) {
    return false;
  }

  return true;
}

bool EventBase::RunInEventBaseThread(const Cob& fn) {
  if (InRunningEventBaseThread()) {
    RunInLoop(fn);
    return true;
  }

  Cob* fn_copy;
  try {
    fn_copy = new Cob(fn);
  } catch (const std::bad_alloc& ex) {
    return false;
  }

  if (!RunInEventBaseThread(&EventBase::RunFunctionPtr, fn_copy)) {
    delete fn_copy;
    return false;
  }

  return true;
}

bool EventBase::RunInEventBaseThreadAndWait(const Cob& fn) {

  if (InRunningEventBaseThread()) {
      LOG(ERROR) << "EventBase " << this << ": Waiting in the event loop is not "
                 << "allowed";
    return false;
  }
  
  bool ready = false;
  std::mutex m;
  std::condition_variable cv;
  RunInEventBaseThread([&] {
        SCOPE_EXIT {
          std::unique_lock<std::mutex> l(m);
          ready = true;
          l.unlock();
          cv.notify_one();
        };
        fn();
  });
  std::unique_lock<std::mutex> l(m);
  cv.wait(l, [&] { return ready; });
  
  return true;
}

bool EventBase::RunImmediatelyOrRunInEventBaseThreadAndWait(const Cob& fn) {
  if (IsInEventBaseThread()) {
    fn();
    return true;
  } else {
    return RunInEventBaseThreadAndWait(fn);
  }
}
  
void EventBase::RunAfterDelay(const Cob& cob,
                              int milliseconds,
                              TimeoutManager::InternalEnum in) {
  if (!TryRunAfterDelay(cob, milliseconds, in)) {
      //folly::throwSystemError(
      //  "error in EventBase::runAfterDelay(), failed to schedule timeout");
  }
}
  
bool EventBase::TryRunAfterDelay(const Cob& cob,
                                 int milliseconds,
                                 TimeoutManager::InternalEnum in) {
  CobTimeout* timeout = new CobTimeout(this, cob, in);
  if (!timeout->ScheduleTimeout(milliseconds)) {
    delete timeout;
    return false;
  }
  pending_cob_timeouts_.push_back(*timeout);
  return true;
}

bool EventBase::RunLoopCallbacks(bool set_context) {
  if (!loop_callbacks_.empty()) {
    BumpHandlingTime();
    LoopCallbackList current_callbacks;
    current_callbacks.swap(loop_callbacks_);
    run_once_callbacks_ = &current_callbacks;

    while (!current_callbacks.empty()) {
      LoopCallback* callback = &current_callbacks.front();
      current_callbacks.pop_front();
      if (set_context) {
        RequestContext::SetContext(callback->context_);
      }
      callback->RunLoopCallback();
    }
 
    run_once_callbacks_ = nullptr;
    return true;
  }
  return false;
}

void EventBase::InitNotificationQueue() {
  queue_.reset(new NotificationQueue<std::pair<void (*)(void*), void*>>());
  fn_runner_.reset(new FunctionRunner());
  fn_runner_->StartConsumingInternal(this, queue_.get());
}

void EventBase::SmoothLoopTime::SetTimeInterval(uint64_t time_interval) {
  exp_coeff_ = -1.0 / time_interval;
}

void EventBase::SmoothLoopTime::Reset(double value) {
  value_ = value;
}

void EventBase::SmoothLoopTime::AddSample(int64_t idle, int64_t busy) {
  enum BusySamplePosition {
    RIGHT = 0,
    CENTER = 1,
    LEFT = 2,
  };
  idle += old_busy_leftover_ + busy;
  old_busy_leftover_ = (busy * BusySamplePosition::CENTER) / 2;
  idle -= old_busy_leftover_;
  
  double coeff = exp(idle * exp_coeff_);
  value_ *= coeff;
  value_ += (1.0 - coeff) * busy;
}

bool EventBase::NothingHandledYet() {
  return (next_loop_cnt_ != latest_loop_cnt_);
}

// static
void EventBase::RunFunctionPtr(Cob* fn) {
  try {
    (*fn)();
  } catch (const std::exception &ex) {
    LOG(ERROR) << "runInEventBaseThread() std::function threw a "
                 << typeid(ex).name() << " exception: " << ex.what();
    abort();
  } catch (...) {
    LOG(ERROR) << "runInEventBaseThread() std::function threw an exception";
    abort();
  }
  delete fn;
}

EventBase::RunInLoopCallback::RunInLoopCallback(void (*fn)(void*), void* arg)
    : fn_(fn) , 
      arg_(arg) {}
  
void EventBase::RunInLoopCallback::RunLoopCallback() noexcept {
  fn_(arg_);
  delete this;
}

void EventBase::AttachTimeoutManager(AsyncTimeout* obj,
                                     InternalEnum internal) {
  
  struct event* ev = obj->GetEvent();
  assert(ev->ev_base == nullptr);
  
  event_base_set(GetLibeventBase(), ev);
  if (internal == AsyncTimeout::InternalEnum::INTERNAL) {
    ev->ev_flags |= EVLIST_INTERNAL;
  }
}
  
void EventBase::DetachTimeoutManager(AsyncTimeout* obj) {
  CancelTimeout(obj);
  struct event* ev = obj->GetEvent();
  ev->ev_base = nullptr;
}
  
bool EventBase::ScheduleTimeout(AsyncTimeout* obj,
                                std::chrono::milliseconds timeout) {
  assert(IsInEventBaseThread());
  struct timeval tv;
  tv.tv_sec = timeout.count() / 1000LL;
  tv.tv_usec = (timeout.count() % 1000LL) * 1000LL;
  
  struct event* ev = obj->GetEvent();
  if (event_add(ev, &tv) < 0) {
    LOG(ERROR) << "EventBase: failed to schedule timeout: " << strerror(errno);
    return false;
  }
  
  return true;
}
  
void EventBase::CancelTimeout(AsyncTimeout* obj) {
  assert(IsInEventBaseThread());
  struct event* ev = obj->GetEvent();
  if (EventUtil::IsEventRegistered(ev)) {
    event_del(ev);
  }
}

#if 0
void EventBase::SetName(const std::string& name) {
  assert(IsInEventBaseThread());
  name_ = name;
  
  if (IsRunning()) {
    SetThreadName(loop_thread_.load(std::memory_order_relaxed),
                  name_);
    }
}
#endif
  
const std::string& EventBase::GetName() {
  assert(IsInEventBaseThread());
  return name_;
}
  
const char* EventBase::GetLibeventVersion() { return event_get_version(); }
const char* EventBase::GetLibeventMethod() { return event_get_method(); }

} // namespace fb
