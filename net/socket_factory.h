#ifndef NET_SOCKET_FACTORY_H_
#define NET_SOCKET_FACTORY_H_
#include "net/socket.h"
#include "net/async_socket.h"

namespace net {

class SocketFactory {
 public:
  virtual ~SocketFactory() {}

  virtual Socket* CreateSocket(int type) = 0;
  virtual Socket* CreateSocket(int family, int type) = 0;
  virtual AsyncSocket* CreateAsyncSocket(int type) = 0;
  virtual AsyncSocket* CreateAsyncSocket(int family, int type) = 0;
};

} // namespace net
#endif // NET_SOCKET_FACTORY_H_
