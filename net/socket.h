#ifndef NET_SOCKET_H_
#define NET_SOCKET_H_
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#define SOCKET_EACCES EACCES

#include "base/macros.h"
#include "net/socket_address.h"


#define INVALID_SOCKET (-1)
#define SOCKET_ERROR   (-1)
#define closesocket(s) close(s)


namespace net {

inline bool IsBlockingError(int e) {
  return (e == EWOULDBLOCK) || (e == EAGAIN) || (e == EINPROGRESS);
}
  
struct SentPacket {
  SentPacket() : packet_id(-1), send_time_ms(-1) {}
  SentPacket(int packet_id, int64_t send_time_ms)
      : packet_id(packet_id), send_time_ms(send_time_ms) {}
  
  int packet_id;
  int64_t send_time_ms;
};


// General interface for the socket implementations of various networks.
// The methods match those of normal UNIX sockets very closely.
class Socket {
 public:
  virtual ~Socket() {}

  virtual SocketAddress GetLocalAddress() const = 0;
  virtual SocketAddress GetRemoteAddress() const = 0;
  
  virtual int Bind(const SocketAddress& addr) = 0;
  virtual int Connect(const SocketAddress& addr) = 0;
  virtual int Send(const void* pv, size_t cb) = 0;
  virtual int SendTo(const void* pv, size_t cb, const SocketAddress& addr) = 0;
  virtual int Recv(void* pv, size_t cb, int64_t* timestamp) = 0;
  virtual int RecvFrom(void* pv,
                       size_t cb,
                       SocketAddress* paddr,
                       int64_t* timestamp) = 0;
  virtual int Listen(int backlog) = 0;
  virtual Socket* Accept(SocketAddress* paddr) = 0;
  virtual int Close() = 0;
  virtual int GetError() const = 0;
  virtual void SetError(int error) = 0;
  inline bool IsBlocking() const { return IsBlockingError(GetError()); }

  enum ConnState {
    CS_CLOSED,
    CS_CONNECTING,
    CS_CONNECTED
  };
  virtual ConnState GetState() const = 0;

  virtual int EstimateMTU(uint16_t* mtu) = 0;
  
  enum Option {
    OPT_DONTFRAGMENT,
    OPT_RCVBUF,
    OPT_SNDBUF,
    OPT_NODELAY,
    OPT_IPV6_V6ONLY,
    OPT_DSCP,
//    OPT_RTP_SENDTIME_EXTN_ID,
  };
  virtual int GetOption(Option opt, int* value) = 0;
  virtual int SetOption(Option opt, int value) = 0;

 protected:
  Socket() {}

 private:
  DISALLOW_COPY_AND_ASSIGN(Socket);
}; 

} // namespace net
#endif // NET_SOCKET_H_
