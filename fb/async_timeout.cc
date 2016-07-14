#include "fb/async_timeout.h"
#include "fb/event_base.h"
#include "fb/event_util.h"
#include "fb/request.h"

#include <assert.h>
#include <glog/logging.h>

namespace fb {

AsyncTimeout::AsyncTimeout(TimeoutManager* timeout_manager)
    : timeout_manager_(timeout_manager) {

  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::LibeventCallback, this);
  event_.ev_base = nullptr;
  timeout_manager_->AttachTimeoutManager(
      this,
      TimeoutManager::InternalEnum::NORMAL);
  RequestContext::GetStaticContext();
}

AsyncTimeout::AsyncTimeout(EventBase* eventBase)
    : timeout_manager_(eventBase) {

  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::LibeventCallback, this);
  event_.ev_base = nullptr;
  if (eventBase) {
    timeout_manager_->AttachTimeoutManager(
      this,
      TimeoutManager::InternalEnum::NORMAL);
  }
  RequestContext::GetStaticContext();
}

AsyncTimeout::AsyncTimeout(TimeoutManager* timeoutManager,
                           InternalEnum internal)
    : timeout_manager_(timeoutManager) {

  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::LibeventCallback, this);
  event_.ev_base = nullptr;
  timeout_manager_->AttachTimeoutManager(this, internal);
  RequestContext::GetStaticContext();
}

AsyncTimeout::AsyncTimeout(EventBase* eventBase, InternalEnum internal)
    : timeout_manager_(eventBase) {

  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::LibeventCallback, this);
  event_.ev_base = nullptr;
  timeout_manager_->AttachTimeoutManager(this, internal);
  RequestContext::GetStaticContext();
}

AsyncTimeout::AsyncTimeout(): timeout_manager_(nullptr) {
  event_set(&event_, -1, EV_TIMEOUT, &AsyncTimeout::LibeventCallback, this);
  event_.ev_base = nullptr;
  RequestContext::GetStaticContext();
}

AsyncTimeout::~AsyncTimeout() {
  CancelTimeout();
}

bool AsyncTimeout::ScheduleTimeout(std::chrono::milliseconds timeout) {
  assert(timeout_manager_ != nullptr);
  context_ = RequestContext::SaveContext();
  return timeout_manager_->ScheduleTimeout(this, timeout);
}

bool AsyncTimeout::ScheduleTimeout(uint32_t milliseconds) {
  return ScheduleTimeout(std::chrono::milliseconds(milliseconds));
}

void AsyncTimeout::CancelTimeout() {
  if (IsScheduled()) {
    timeout_manager_->CancelTimeout(this);
  }
}

bool AsyncTimeout::IsScheduled() const {
  return EventUtil::IsEventRegistered(&event_);
}

void AsyncTimeout::AttachTimeoutManager(
    TimeoutManager* timeoutManager,
    InternalEnum internal) {
  // This also implies no timeout is scheduled.
  assert(timeout_manager_ == nullptr);
  assert(timeoutManager->IsInTimeoutManagerThread());
  timeout_manager_ = timeoutManager;

  timeout_manager_->AttachTimeoutManager(this, internal);
}

void AsyncTimeout::AttachEventBase(
    EventBase* eventBase,
    InternalEnum internal) {
  AttachTimeoutManager(eventBase, internal);
}

void AsyncTimeout::DetachTimeoutManager() {
  // Only allow the event base to be changed if the timeout is not
  // currently installed.
  if (IsScheduled()) {
    // Programmer bug.  Abort the program.
    LOG(ERROR) << "detachEventBase() called on scheduled timeout; aborting";
    abort();
    return;
  }

  if (timeout_manager_) {
    timeout_manager_->DetachTimeoutManager(this);
    timeout_manager_ = nullptr;
  }
}

void AsyncTimeout::DetachEventBase() {
  DetachTimeoutManager();
}

void AsyncTimeout::LibeventCallback(int fd, short events, void* arg) {
  AsyncTimeout* timeout = reinterpret_cast<AsyncTimeout*>(arg);
  assert(fd == -1);
  assert(events == EV_TIMEOUT);

  (void)fd;
  (void)events;

  LOG(INFO) << "ev_flags: " << timeout->event_.ev_flags;
  // double check that ev_flags gets reset when the timeout is not running
  assert((timeout->event_.ev_flags & ~EVLIST_INTERNAL) == EVLIST_INIT);

  // this can't possibly fire if timeout->eventBase_ is nullptr
  (void) timeout->timeout_manager_->BumpHandlingTime();

  auto old_ctx =
    RequestContext::SetContext(timeout->context_);

  timeout->TimeoutExpired();

  RequestContext::SetContext(old_ctx);
}

} // fb
