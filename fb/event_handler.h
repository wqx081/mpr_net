#ifndef FB_EVENT_HANDLER_H_
#define FB_EVENT_HANDLER_H_
#include "base/macros.h"
#include "fb/event_util.h"

#include <stddef.h>
#include <glog/logging.h>

namespace fb {

class EventBase;

class EventHandler {
 public:
  enum EventFlags {
    NONE = 0,
    READ = EV_READ,
    WRITE = EV_WRITE,
    READ_WRITE = (READ | WRITE),
    PERSIST = EV_PERSIST    
  };

  explicit EventHandler(EventBase* event_base=nullptr, int fd = -1);
  virtual ~EventHandler();

  virtual void HandlerReady(uint16_t events) noexcept = 0;
  bool RegisterHandler(uint16_t events) {
    return RegisterImpl(events, false);
  }
  void UnregisterHandler();

  bool IsHandlerRegistered() const {
    return EventUtil::IsEventRegistered(&event_);
  }

  void AttachEventBase(EventBase* event_base);
  void DetachEventBase();
  void ChangeHandlerFD(int fd);
  void InitHandler(EventBase* event_base, int fd);
  uint16_t GetRegisteredEvents() const {
    return (IsHandlerRegistered()) ? event_.ev_events : 0;
  }

  bool RegisterInternalHandler(uint16_t events) {
    return RegisterImpl(events, true);
  }

  bool IsPending() const;

 private:
  bool RegisterImpl(uint16_t events, bool internal);
  void EnsureNotRegistered(const char* fn);
  void SetEventBase(EventBase* fn);

  static void LibeventCallback(int fd, short events, void* arg);

  struct event event_;
  EventBase* event_base_;

  DISALLOW_COPY_AND_ASSIGN(EventHandler);
};

} // namespace fb
#endif // FB_EVENT_HANDLER_H_
