#include "net/logging.h"
#include "net/async_socket.h"
#include "net/physical_socket_server.h"
#include "net/thread.h"

#include <memory>

using namespace base;
using namespace net;

class EchoSocketDispatcher : public SocketDispatcher {
 public:
  explicit EchoSocketDispatcher(PhysicalSocketServer* ss)
      : SocketDispatcher(ss) {
  }
  EchoSocketDispatcher(SOCKET s, PhysicalSocketServer* ss)
      : SocketDispatcher(s, ss) {
  }

 protected:
  SOCKET DoAccept(SOCKET socket,
		  sockaddr* addr,
		  socklen_t* addrlen) override;
  int DoSend(SOCKET socket,
             const char* buf,
	     int len,
	     int flags) override;
  int DoSendTo(SOCKET socket,
               const char* buf,
	       int len,
	       int flags,
	       const struct sockaddr* dest_addr,
	       socklen_t addrlen) override;
};

class EchoPhysicalSocket;

class EchoPhysicalSocketServer : public PhysicalSocketServer {
 public:
  explicit EchoPhysicalSocketServer(EchoPhysicalSocket* test)
      : test_(test) {
  }

  AsyncSocket* CreateAsyncSocket(int type) override {
    SocketDispatcher* dispatcher = new EchoSocketDispatcher(this);
    if (!dispatcher->Create(type)) {
      delete dispatcher;
      return nullptr;
    }
    return dispatcher;
  }

  AsyncSocket* CreateAsyncSocket(int family, int type) override {
    SocketDispatcher* dispatcher = new EchoSocketDispatcher(this);
    if (!dispatcher->Create(family, type)) {
      delete dispatcher;
      return nullptr;
    }
    return dispatcher;
  }

  AsyncSocket* WrapSocket(SOCKET s) override {
    SocketDispatcher* dispatcher = new EchoSocketDispatcher(s, this);
    if (!dispatcher->Initialize()) {
      delete dispatcher;
      return nullptr;
    }
    return dispatcher;
  }

 private:
  EchoPhysicalSocket* test_;
};

SOCKET EchoSocketDispatcher::DoAccept(SOCKET socket,
		                      sockaddr* addr,
				      socklen_t* addrlen) {
  EchoPhysicalSocketServer* ss = static_cast<EchoPhysicalSocketServer*>(socketserver());
  (void)ss;
  //TODO
  return SocketDispatcher::DoAccept(socket, addr, addrlen); 
}

int EchoSocketDispatcher::DoSend(SOCKET socket,
		                 const char* buf,
				 int len,
				 int flags) {
  return SocketDispatcher::DoSend(socket, buf, len, flags);
}

int EchoSocketDispatcher::DoSendTo(SOCKET socket,
		                   const char* buf,
				   int len,
				   int flags,
				   const struct sockaddr* dest_addr,
				   socklen_t addrlen) {
  return SocketDispatcher::DoSendTo(socket, buf, len, flags, dest_addr, addrlen);

}

class EchoPhysicalSocket {
 public:
  EchoPhysicalSocket() : server_(new EchoPhysicalSocketServer(this)),
    scope_(server_.get()),
    fail_accept_(false),
    max_send_size_(-1) {
  }

 protected:
  void ConnectInternalAcceptError(const IPAddress& loopback);
  void WritableAfterPartialWrite(const IPAddress& loopback);

  std::unique_ptr<EchoPhysicalSocketServer> server_;
  SocketServerScope scope_;
  bool fail_accept_;
  int max_send_size_;
};

int main() {
  
  
  return 0;
}
