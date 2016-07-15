#ifndef FB_ASYNC_SOCKET_BASE_H_
#define FB_ASYNC_SOCKET_BASE_H_
#include "net/socket_address.h"
#include "fb/event_base.h"

namespace fb {

class AsyncSocketBase {
 public:
  virtual EventBase* GetEventBase() const = 0;
  virtual ~AsyncSocketBase() {}
  virtual void GetAddress(net::SocketAddress* ) const = 0;
};

} // namespace fb
#endif // FB_ASYNC_SOCKET_BASE_H_
