#ifndef FB_ASYNC_SOCKET_H_
#define FB_ASYNC_SOCKET_H_
#include <sys/types.h>
#include <sys/socket.h>
#include "net/socket_address.h"

#include "fb/async_timeout.h"
#include "fb/async_transport.h"
#include "fb/event_handler.h"
#include "fb/delayed_destruction.h"
#include "fb/shutdown_socket_set.h"

#include <memory>
#include <map>

namespace fb {

class AsyncSocket : virtual public AsyncTransportWrapper {
 public:
  typedef std::unique_ptr<AsyncSocket, Destructor> UniquePtr;

  class ConnectCallback {
   public:
    virtual ~ConnectCallback() {}
    virtual void ConnectSuccess() noexcept = 0;
    //TODO:
//  virtual void ConnectError(const AsyncSocketException& ex);
  };
  explicit AsyncSocket();
  explicit AsyncSocket(EventBase* event_base);
  
  AsyncSocket(EventBase* event_base,
              const net::SocketAddress& address,
              uint32_t connect_timeout = 0);
  AsyncSocket(EventBase* event_base,
              const std::string& ip,
              uint16_t port,
              uint32_t connect_timeout = 0);
  AsyncSocket(EventBase* event_base, int fd);
  
  static std::shared_ptr<AsyncSocket> NewSocket(EventBase* event_base) {
    return std::shared_ptr<AsyncSocket>(new AsyncSocket(event_base), Destructor());
  }

  static std::shared_ptr<AsyncSocket> NewSocket(EventBase* event_base,
                                                const net::SocketAddress& address,
                                                uint32_t connect_timeout = 0) {
    return std::shared_ptr<AsyncSocket>(new AsyncSocket(event_base, address, connect_timeout),
                                        Destructor());
  }

  static std::shared_ptr<AsyncSocket> NewSocket(EventBase* event_base, int fd) {
    return std::shared_ptr<AsyncSocket>(new AsyncSocket(event_base, fd),
                                        Destructor());
  }

  virtual void Destroy() override;
  EventBase* GetEventBase() const override {
    return event_base_;
  }
  virtual int GetFd() const { return fd_; }
  virtual int DetachFd();
  class OptionKey {
  };
  typedef std::map<OptionKey, int> OptionMap;
  
  static const OptionMap empty_option_map;
  static const net::SocketAddress& AnyAddress();
  
  virtual void Connect(ConnectCallback* callback,
                       const net::SocketAddress& address,
                       int timeout = 0,
                       const OptionMap& options = empty_option_map,
                       const net::SocketAddress& bind_address = AnyAddress());
  virtual void Connect(ConnectCallback* callback,
                       const std::string& ip,
                       uint16_t port,
                       int timeout = 00,
                       const OptionMap& options = empty_option_map) noexcept;
  void CancelConnect();
  void SetSendTimeout(uint32_t milliseconds) override;
  uint32_t GetSendTimeout() const override;
  void SetMaxReadsPerEvent(uint16_t max_reads);
  uint16_t GetMaxReadsPerEvent() const;

  void SetReadCB(ReadCallback* callback) override;
  ReadCallback* GetReadCallback() const override;

  void Write(WriteCallback* callback,
             const void* buf,
             size_t bytes,
             WriteFlags flags = WriteFlags::NONE) override;
  void Writev(WriteCallback* callback,
              const iovec* vec,
              size_t count,
              WriteFlags flags = WriteFlags::NONE) override;
  
  // AsyncTransport
  void Close() override;
  void CloseNow() override;
  void CloseWithReset() override;
  void ShutdownWrite() override;
  void ShutdownWriteNow() override;

  bool Readable() const override;
  bool IsPending() const override;
  virtual bool Hangup() const;
  bool Good() const override;
  bool Error() const override;
  void AttachEventBase(EventBase* event_base) override;
  void DetachEventBase() override;
  bool IsDetachable() const override;

  virtual GetLocalAddress(net::SocketAddress* address) const override;
  virtual GetPeerAddress(net::SocketAddress* address) const override;

  bool IsEorTrackingEnable() const override;
  void SetEorTracking(bool track) override;
  bool Connecting() const override;
  size_t GetAppBytesWritten() const override;
  size_t GetRawBytesWritten() const override;
  size_t GetAppBytesReceived() const override;
  size_t GetRawBytesReceived() const override;
  
  int SetNoDelay(bool no_delay);
  void SetCloseOnExec();
  int SetCongestionFlavor(const std::string& cname);
  int SetQuickAck(bool quick_ack);
  int SetSendBufferSize(size_t buf_size);
  int SetRecvBufferSize(size_t buf_size);
  #define SO_SET_NAMESPACE 41
  int SetTCPProfile(int profd);

  template<typename T>
  int GetSockOpt(int level, int optname, T* optval, socklen_t* optlen) {
    return getsockopt(fd_, level, optname, (void*) optval, optlen);
  }
  template<typename T>
  int SetSockOpt(int level, int optname, const T* optval) {
    return setsockopt(fd_, level, optname, optval, sizeof(T));
  }

  enum class StateEnum : uint8_t {
    UNINIT,
    CONNECTING,
    ESTABLISHED,
    CLOSED,
    ERROR
  };

 protected:
  enum ReadResultEnum {
    READ_EOF = 0,
    READ_ERROR = -1,
    READ_BLOCKING = -2,
  };

  ~AsyncSocket();
  friend std::ostream& operator<<(std::ostream& os, const StateEnum& state);

  enum ShutdownFlags {
    SHUT_WRITE_PENDING = 0x01,
    SHUT_WRITE = 0x02,
    SHUT_READ = 0x04,
  };

  class WriteRequest;

  class WriteTimeout : public AsyncTimeout {
   public:
    WriteTimeout(AsyncSocket* socket, EventBase* event_base)
        : AsyncTimeout(event_base),
          socket_(socket) {}
    virtual void TimeoutExpired() noexcept {
      socket_->TimeoutExpired();
    }

   private:
    AsyncSocket* socket_;
  };

  class IoHandler : public EventHandler {
   public:
    IoHandler(AsyncSocket* socket, EventBase* event_base)
        : EventHandler(event_base, -1),
          socket_(socket) {}
    IoHandler(AsyncSocket* socket, EventBase* event_base, int fd)
        : EventHandler(event_base, fd),
          socket_(socket) {}

    virtual void HandlerReady(uint16_t events) noexcept {
      socket_->IoReady(events);
    }

   private:
    AsyncSocket* socket_;
  };

  void Init();

  // event notification methods.
  void IoReady(uint16_t events) noexcept;
  virtual void CheckForImmediateRead() noexcept;
  virtual void HandleInitialReadWrite() noexcept;
  virtual void HandleRead() noexcept;
  virtual void HandleWrite() noexcept;
  virtual void HandleConnect() noexcept;
  void TimeoutExpired() noexcept;

  virtual ssize_t PerformRead(void* buf, size_t buflen);
  //TODO
  virtual ssize_t PerformWrite(const iovec* vec, uint32_t count,
                               WriteFlags flags, uint32_t* count_written,
                               uint32_t* partial_written);
  
  bool UpdateEventRegistration();
  bool UpdateEventRegistration(uint16_t enable, uint16_t disable);

  void DoClose();

  // Error Handling methods
  void StartFail();
  void FinishFail();
//  void Fail(const char* fn, AsyncSocketException& ex);
  
  StateEnum state_;
  uint8_t shutdown_flags_;
  uint16_t event_flags_;
  int fd_;
  mutable net::SocketAddress addr_;
  uint32_t send_timeout_;
  uint16_t max_reads_per_event_;
  EventBase* event_base_;
  WriteTimeout write_timeout_;
  ioHandler io_handler_;

  ConnectCallback* connect_callback_;
  ReadCallback* read_callback_;
  WriteRequest* write_request_head_;
  WriteRequest* write_request_tail_;
  ShutdownSocket* shutdown_socket_set_;
  size_t app_bytes_received_;
  size_t app_bytes_written_;
};

} // namespace fb
#endif // FB_ASYNC_SOCKET_H_
