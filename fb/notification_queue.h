#ifndef FB_NOTIFICATION_QUEUE_H_
#define FB_NOTIFICATION_QUEUE_H_

#include "base/macros.h"

#include "fb/event_base.h"
#include "fb/event_handler.h"
#include "fb/request.h"
#include "fb/scope_guard.h"
#include "fb/spin_lock.h"

#include <fcntl.h>
#include <unistd.h>

#include <glog/logging.h>
#include <deque>

namespace fb {

template<typename MESSAGE>
class NotificationQueue {
 public:
 private:
  inline bool CheckQueueSize(size_t max_size, bool throws=true) const;
  inline bool CheckDraining(bool thorws=true);

  DISALLOW_COPY_AND_ASSIGN(NotificationQueue);
};

} // namespace fb
#endif // FB_NOTIFICATION_QUEUE_H_
