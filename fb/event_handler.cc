#include "fb/event_base.h"
#include "fb/event_handler.h"

#include <assert.h>

namespace fb {

EventHandler::EventHandler(EventBase* event_base, int fd) {

  LOG(INFO) << "fd----: " << fd;

  event_set(&event_, fd, 0, &EventHandler::LibeventCallback, this);
  if (event_base != nullptr) {
    SetEventBase(event_base);
  } else {
    event_.ev_base = nullptr;
    event_base_ = nullptr;
  }
}

EventHandler::~EventHandler() {
  UnregisterHandler();
}

bool EventHandler::RegisterImpl(uint16_t events, bool internal) {
  assert(event_.ev_base != nullptr);

  if (IsHandlerRegistered()) {
    if (events == event_.ev_events &&
        static_cast<bool>(event_.ev_flags & EVLIST_INTERNAL) == internal) {
      return true;
    }
    event_del(&event_);
  }

  struct event_base* evb = event_.ev_base;
  event_set(&event_,
            event_.ev_fd,
            events,
            &EventHandler::LibeventCallback,
            this);
  event_base_set(evb, &event_);

  if (internal) {
    event_.ev_flags |= EVLIST_INTERNAL;
  }

  if (event_add(&event_, nullptr) < 0) {
   LOG(ERROR) << "EventBase: failed to register event handler for fd "
              << event_.ev_fd << ": " << strerror(errno);

    event_del(&event_);
    return false;
  }
  return true;
}

void EventHandler::UnregisterHandler() {
  if (IsHandlerRegistered()) {
    event_del(&event_);
  }  
}

void EventHandler::AttachEventBase(EventBase* event_base) {
  assert(event_.ev_base == nullptr);
  assert(!IsHandlerRegistered());
  assert(event_base->IsInEventBaseThread());

  SetEventBase(event_base);
}

void EventHandler::DetachEventBase() {
  EnsureNotRegistered(__func__);
  event_.ev_base = nullptr;
}

void EventHandler::ChangeHandlerFD(int fd) {
  EnsureNotRegistered(__func__);
  struct event_base* evb = event_.ev_base;
  event_set(&event_, fd, 0, &EventHandler::LibeventCallback, this);
  event_.ev_base = evb;
}

void EventHandler::InitHandler(EventBase* event_base, int fd) {
  EnsureNotRegistered(__func__);
  event_set(&event_, fd, 0, &EventHandler::LibeventCallback, this);
  SetEventBase(event_base);
}

void EventHandler::EnsureNotRegistered(const char* fn) {
  if (IsHandlerRegistered()) {
    LOG(ERROR) << fn << " called on registered handler; aborting";
    abort();
  }
}

void EventHandler::LibeventCallback(int fd, short events, void* arg) {
  (void)fd;
  EventHandler* handler = reinterpret_cast<EventHandler*>(arg);
  assert(fd == handler->event_.ev_fd);

  (void) handler->event_base_->BumpHandlingTime();
  handler->HandlerReady(events);
}

void EventHandler::SetEventBase(EventBase* event_base) {
  event_base_set(event_base->GetLibeventBase(), &event_);
  event_base_ = event_base;
}

bool EventHandler::IsPending() const {
  if (event_.ev_flags & EVLIST_ACTIVE) {
    if (event_.ev_res & EV_READ) {
      return true;
    }
  }
  return false;
}

} // namespace fb
