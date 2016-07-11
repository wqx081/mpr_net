#ifndef FB_EVENT_UTIL_H_
#define FB_EVENT_UTIL_H_
#include <event.h>

namespace fb {

class EventUtil {
 public:
  static bool IsEventRegistered(const struct event* ev) {
    enum {
      EVLIST_REGISTERED = (EVLIST_INSERTED |
                           EVLIST_ACTIVE   |
                           EVLIST_TIMEOUT  |
                           EVLIST_SIGNAL)
    };
    return (ev->ev_flags & EVLIST_REGISTERED);
  }
};

} // namespace fb
#endif // FB_EVENT_UTIL_H_
