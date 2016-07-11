#ifndef NET_PHYSICAL_SOCKET_SERVER_H_
#define NET_PHYSICAL_SOCKET_SERVER_H_

#include <memory>
#include <vector>

#include "net/async_file.h"
#include "net/socket_server.h"
#include "base/critical_section.h"
#include "net/net_helpers.h"
#include "net/network_monitor.h"

typedef int SOCKET;

namespace net {

// Event constants for the Dispatcher class.
enum DispatcherEvent {
  DE_READ    = 0x0001,
  DE_WRITE   = 0x0002,
  DE_CONNECT = 0x0004,
  DE_CLOSE   = 0x0008,
  DE_ACCEPT  = 0x0010,
};

class Signaler;
class PosixSignalDispatcher;


class Dispatcher {
 public:
  virtual ~Dispatcher() {}
  virtual uint32_t GetRequestedEvents() = 0;
  virtual void OnPreEvent(uint32_t ff) = 0;
  virtual void OnEvent(uint32_t ff, int err) = 0;
  virtual int GetDescriptor() = 0;
  virtual bool IsDescriptorClosed() = 0;
};

// A socket server that provide the real sockets of the underlying OS.
class PhysicalSocketServer : public SocketServer {
 public:
  PhysicalSocketServer();
  ~PhysicalSocketServer() override;
  
  // SocketFactory:
  Socket* CreateSocket(int type) override;
  Socket* CreateSocket(int family, int type) override;

  AsyncSocket* CreateAsyncSocket(int type) override;
  AsyncSocket* CreateAsyncSocket(int family, int type) override;

  // Internal Factory for Accept (virtual so it can be overwritten in tests).
  virtual AsyncSocket* WrapSocket(SOCKET s);

  // SocketServer:
  bool Wait(int cms, bool process_io) override;
  void WakeUp() override;
  
  void Add(Dispatcher* dispatcher);
  void Remove(Dispatcher* dispatcher);

  AsyncFile* CreateFile(int fd);
  // Sets the function to be executed in response to the specified POSIX
  // signal.
  // The function is executed from inside Wait() using the "self-pipetrick"
  virtual bool SetPosixSignalHandler(int signum, void (*handler)(int));
  
 protected:
  Dispatcher* signal_dispatcher();

 private:
  typedef std::vector<Dispatcher*> DispatcherList;
  typedef std::vector<size_t*> IteratorList;

  static bool InstallSignal(int signum, void (*handler)(int));
  std::unique_ptr<PosixSignalDispatcher> signal_dispatcher_;
  DispatcherList dispatchers_;
  IteratorList iterators_;
  Signaler* signal_wakeup_;
base::CriticalSection crit_;
  bool fWait_;

};

class PhysicalSocket : public AsyncSocket, 
	               public sigslot::has_slots<> {
 public:
  PhysicalSocket(PhysicalSocketServer* ss, SOCKET s = INVALID_SOCKET);
  ~PhysicalSocket() override;
  
  virtual bool Create(int family, int type);
  
  SocketAddress GetLocalAddress() const override;
  SocketAddress GetRemoteAddress() const override;
  
  int Bind(const SocketAddress& bind_addr) override;
  int Connect(const SocketAddress& addr) override;
  
  int GetError() const override;
  void SetError(int error) override;
  
  ConnState GetState() const override;
  
  int GetOption(Option opt, int* value) override;
  int SetOption(Option opt, int value) override;
 
  int Send(const void* pv, size_t cb) override;
  int SendTo(const void* buffer,
             size_t length,
             const SocketAddress& addr) override;
  
  int Recv(void* buffer, size_t length, int64_t* timestamp) override;
  int RecvFrom(void* buffer,
               size_t length,
               SocketAddress* out_addr,
               int64_t* timestamp) override;
  
  int Listen(int backlog) override;
  AsyncSocket* Accept(SocketAddress* out_addr) override;
  
  int Close() override;
  
  int EstimateMTU(uint16_t* mtu) override;
  
  SocketServer* socketserver() { return ss_; }
  
 protected:
  int DoConnect(const SocketAddress& connect_addr);
  
  virtual SOCKET DoAccept(SOCKET socket, sockaddr* addr, socklen_t* addrlen);
  virtual int DoSend(SOCKET socket, const char* buf, int len, int flags);
  virtual int DoSendTo(SOCKET socket, 
		       const char* buf, 
		       int len, 
		       int flags,
                       const struct sockaddr* dest_addr, 
		       socklen_t addrlen);

  void OnResolveResult(AsyncResolverInterface* resolver);
  void UpdateLastError();
  void MaybeRemapSendError();
  static int TranslateOption(Option opt, int* slevel, int* sopt);
    
  PhysicalSocketServer* ss_;
  SOCKET s_;
  uint8_t enabled_events_;
  bool udp_;
  base::CriticalSection crit_;
  int error_; // GUARDED_BY(crit_);
  ConnState state_;
  AsyncResolver* resolver_;
  
#if !defined(NDEBUG)
  std::string dbg_addr_;
#endif
};

class SocketDispatcher : public Dispatcher, 
	                 public PhysicalSocket {
 public:
  explicit SocketDispatcher(PhysicalSocketServer *ss);
  SocketDispatcher(SOCKET s, PhysicalSocketServer *ss);
  ~SocketDispatcher() override;
  
  bool Initialize();
  
  virtual bool Create(int type);
  bool Create(int family, int type) override;
  
  int GetDescriptor() override;
  bool IsDescriptorClosed() override;
  
  uint32_t GetRequestedEvents() override;
  void OnPreEvent(uint32_t ff) override;
  void OnEvent(uint32_t ff, int err) override;
  
  int Close() override;
};

} // namespace net
#endif // NET_PHYSICAL_SOCKET_SERVER_H_
