#ifndef NET_ASYNC_RESOLVER_INTERFACE_H_
#define NET_ASYNC_RESOLVER_INTERFACE_H_

#include "net/sigslot.h"
#include "net/socket_address.h"

namespace net {

class AsyncResolverInterface {
 public:
  AsyncResolverInterface();
  virtual ~AsyncResolverInterface();
  virtual void Start(const SocketAddress& addr) = 0;
  virtual bool GetResolvedAddress(int family, SocketAddress* addr) const = 0;
  virtual int GetError() const = 0;
  virtual void Destroy(bool wait) = 0;
  SocketAddress address() const {
    SocketAddress addr;
    GetResolvedAddress(AF_INET, &addr);
    return addr;
  }
  
  sigslot::signal1<AsyncResolverInterface *> SignalDone;
};

} // namespace net
#endif //NET_ASYNC_RESOLVER_INTERFACE_H_
