#ifndef NET_SOCKET_SERVER_H_
#define NET_SOCKET_SERVER_H_
#include "net/socket_factory.h"

#include <memory>

namespace net {

class MessageQueue;
class NetworkBinderInterface;

class SocketServer : public SocketFactory {
 public:
  static const int kForever = -1;
  static std::unique_ptr<SocketServer> CreateDefault();
  
  virtual void SetMessageQueue(MessageQueue* ) {}
  virtual bool Wait(int cms, bool process_io) = 0;
  virtual void WakeUp() = 0;
  void set_network_binder(NetworkBinderInterface* binder) {
    network_binder_ = binder;
  }
  NetworkBinderInterface* network_binder() const { return network_binder_; }
  
 private:
  NetworkBinderInterface* network_binder_ = nullptr;
};

} // namespace net
#endif // NET_SOCKET_SERVER_H_
