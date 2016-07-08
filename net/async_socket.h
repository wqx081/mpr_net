#ifndef NET_ASYNC_SOCKET_H_
#define NET_ASYNC_SOCKET_H_
#include "net/sigslot.h"
#include "net/socket.h"

namespace net {

class AsyncSocket : public Socket {
 public:
  AsyncSocket();
  ~AsyncSocket() override;

  AsyncSocket* Accept(SocketAddress* addr) override = 0;
  // Ready to read
  sigslot::signal1<AsyncSocket*,
                   sigslot::multi_threaded_local> SignalReadEvent;
  // Ready to write
  sigslot::signal1<AsyncSocket*,
                   sigslot::multi_threaded_local> SignalWriteEvent;
  // connected
  sigslot::signal1<AsyncSocket*> SignalConnectEvent;
  // closed
  sigslot::signal2<AsyncSocket*, int> SignalCloseEvent;
};


class AsyncSocketAdapter : public AsyncSocket,
                           public sigslot::has_slots<> {
 public:
  explicit AsyncSocketAdapter(AsyncSocket* socket);
  ~AsyncSocketAdapter() override;
  void Attach(AsyncSocket* socket);
  SocketAddress GetLocalAddress() const override;
  SocketAddress GetRemoteAddress() const override;
  int Bind(const SocketAddress& addr) override;
  int Connect(const SocketAddress& addr) override;
  int Send(const void* pv, size_t cb) override;
  int SendTo(const void* pv, size_t cb, const SocketAddress& addr) override;
  int Recv(void* pv, size_t cb, int64_t* timestamp) override;
  int RecvFrom(void* pv,
               size_t cb,
               SocketAddress* paddr,
               int64_t* timestamp) override;

  int Listen(int backlog) override;
  AsyncSocket* Accept(SocketAddress* paddr) override;
  int Close() override;
  int GetError() const override;
  void SetError(int error) override;
  ConnState GetState() const override;
  int EstimateMTU(uint16_t* mtu) override;
  int GetOption(Option opt, int* value) override;
  int SetOption(Option opt, int value) override;

 protected: 
  virtual void OnConnectEvent(AsyncSocket* socket);
  virtual void OnReadEvent(AsyncSocket* socket);
  virtual void OnWriteEvent(AsyncSocket* socket);
  virtual void OnCloseEvent(AsyncSocket* socket, int err);

  AsyncSocket* socket_;
};

} // namespace net
#endif // NET_ASYNC_SOCKET_H_
