#ifndef FB_AYSNC_SERVER_SOCKET_H_
#define FB_AYSNC_SERVER_SOCKET_H_

#include "fb/delayed_destruction.h"
#include "fb/event_handler.h"
//#include "fb/notification_queue.h"
#include "fb/async_timeout.h"
#include "fb/async_socket_base.h"
#include "net/socket_address.h"
#include "fb/shutdown_socket_set.h"

#include <memory>
#include <exception>
#include <vector>
#include <limits.h>
#include <stddef.h>
#include <sys/socket.h>

namespace fb {

class AsyncServerSocket : public DelayedDestruction,
                          public AsyncSocketBase {
 public:
  typedef std::unique_ptr<AsyncServerSocket, Destructor> UniquePtr;
  AsyncServerSocket(AsyncServerSocket&&) = delete;

  class AcceptCallback {
   public: 
    virtual ~AcceptCallback() {}
    virtual void ConnectionAccepted(int fd,
                                    const net::SocketAddress& client_addr) noexcept=0;
    virtual void AcceptError(const std::exception& ex) noexcept = 0;
    virtual void AcceptStarted() noexcept {}
    virtual void AcceptStopped() noexcept {}
  };
  
  static const uint32_t kDefaultMaxAcceptAtOnce = 30;
  static const uint32_t kDefaultCallbackAcceptAtOnce = 5;
  static const uint32_t kDefaultMaxMessagesInQueue = 0;

  explicit AsyncServerSocket(EventBase* event_base = nullptr);
  static std::shared_ptr<AsyncServerSocket> NewSocket(EventBase* event_base=nullptr) {
    return std::shared_ptr<AsyncServerSocket>(new AsyncServerSocket(event_base),
                                              Destructor());
  }
  void SetShutdownSocketSet(ShutdownSocketSet* ss);
  virtual void Destroy();
  void AttachEventBase(EventBase* event_base);
  void DetachEventBase();
  EventBase* GetEventBase() const {
    return event_base_;
  }

  void UseExistingSocket(int fd);
  void UseExistingSockets(const std::vector<int>& fds);

  std::vector<int> GetSockets() const {
    std::vector<int> sockets;
    for (auto& handler : sockets_) {
      sockets.push_back(handler.socket_);
    }
    return sockets;
  }
  int GetSocket() const {
    if (sockets_.size() > 1) {
    }
    if (sockets_.size() == 0) {
      return -1;
    } else {
      return sockets_[0].socket_;
    }
  }

  virtual void Bind(const net::SocketAddress& address);
  virtual void Bind(const std::vector<net::IPAddress>& ip_addresses,
                    uint16_t port);
  virtual void Bind(uint16_t port);

  void GetAddress(net::SocketAddress* address) const;
  std::vector<net::SocketAddress> GetAddresses() const;
  
  virtual void Listen(int backlog);
  virtual void AddAcceptCallback(AcceptCallback* callback,
                                 EventBase* event_base,
                                 uint32_t max_at_once = kDefaultCallbackAcceptAtOnce);
  void RemoveAcceptCallback(AcceptCallback* callback, EventBase* event_base);
  virtual void StartAccepting();
  void PauseAccepting();
  int StopAccepting(int shutdown_flags = -1);

  uint32_t GetMaxAcceptAtOnce() const {
    return max_accept_at_once_;
  }
  void SetMaxAcceptAtOnce(uint32_t num_conns) {
    max_accept_at_once_ = num_conns;
  }
  uint32_t GetMaxNumMessagesInQueue() const {
    return max_num_msgs_in_queue_;
  }
  void SetMaxNumMessagesInQueue(uint32_t num) {
    max_num_msgs_in_queue_ = num;
  }
  double GetAcceptRateAdjuestSpeed() const {
    return accept_rate_adjuest_speed_;
  }
  void SetAcceptRateAdjustSpeed(double speed) {
    accept_rate_adjuest_speed_ = speed;
  }
  uint64_t GetNumDroppedConnections() const {
    return num_dropped_connections_;
  }

  void SetKeepAliveEnabled(bool enabled) {
    keep_alive_enabled_ = enabled;
    for (auto& handler : sockets_) {
      if (handler.socket_ < 0) {
        continue;
      }
      int val = enabled ? 1 : 0;
      if (setsockopt(handler.socket_, SOL_SOCKET, SO_KEEPALIVE, &val, sizeof(val)) != 0) {
      }
    }
  }
  bool GetKeepAliveEnabled() const {
    return keep_alive_enabled_;
  }

  void SetReusePortEnabled(bool enabled) {
    reuse_port_enabled_ = enabled;
    for (auto& handler : sockets_) {
      if (handler.socket_ < 0) {
        continue;
      }
      int val = enabled ? 1 : 0;
      if (setsockopt(handler.socket_, SOL_SOCKET, SO_REUSEPORT, &val, sizeof(val)) != 0) {
      }
    }
  }
  bool GetReusePortEnabled() const {
    return reuse_port_enabled_;
  }
  void SetCloseOnExec(bool close_on_exec) {
    close_on_exec_ = close_on_exec;
  }
  bool GetCloseOnExec() const {
    return close_on_exec_;
  }

 protected:
  virtual ~AsyncServerSocket();

 private:
  enum class MessageType {
    MSG_NEW_CONN = 0,
    MSG_ERROR = 1,
  };
  
  struct QueueMessage {
    MessageType type;
    int fd;
    int error;
    net::SocketAddress address;
    std::string msg;
  };

  class RemoteAcceptor : private NotificationQueue<QueueMessage>::Consumer {
   public:
    explicit RemoteAcceptor(AcceptCallback* callback)
        : callback_(callback) {}
    ~RemoteAcceptor() {}
   
    void Start(EventBase* event_base, uint32_t max_at_once, uint32_t max_in_queue);
    void Stop(EventBase* event_base, AcceptCallback* callback);

    virtual void MessageAvailable(QueueMessage&& message);

    NotificationQueue<QueueMessage>* GetQueue() {
      return &queue_;
    }

   private:
    AcceptCallback* callback_;
    NotificationQueue<QueueMessage> queue_;
  };

  struct CallbackInfo {
    CallbackInfo(AcceptCallback* cb, EventBase* in_event_base)
        : callback(cb),
          event_base(in_event_base),
          consumer(nullptr) {}

    AcceptCallback* callback;
    EventBase* event_base;
    RemoteAcceptor* consumer;
  };

  class BackoffTimeout;

  virtual void HandlerReady(uint16_t events,
                            int socket,
                            sa_family_t family) noexcept;
  int CreateSocket(int family);
  void SetupSocket(int fd);
  void BindSocket(int fd, const net::SocketAddress& address, bool is_existing_socket);
  void DispatchSocket(int socket, net::SocketAddress&& address);
  void DispatchError(const char* msg, int errno_value);
  void EnterBackoff();
  void BackoffTimeoutExpired();
  
  CallbackInfo* NextCallback() {
    CallbackInfo* info = &callbacks_[callback_index_];
   
    ++callback_index_;
    if (callback_index_ >= callbacks_.size()) {
      callback_index_ = 0;
    }

    return info;
  }

  struct ServerEventHandler : public EventHandler {
    ServerEventHandler(EventBase* event_base,
                       int socket,
                       AsyncServerSocket* parent,
                       sa_family_t address_family)
        : EventHandler(event_base, socket),
          event_base_(event_base),
          socket_(socket),
          parent_(parent),
          address_family_(address_family) {}

    ServerEventHandler(const ServerEventHandler& other)
        : EventHandler(other.event_base_, other.socket_),
          event_base_(other.event_base_),
          socket_(other.socket_),
          parent_(other.parent_),
          address_family_(other.address_family_) {}

    ServerEventHandler& operator=(const ServerEventHandler& other) {
      if (this != &other) {
        event_base_ = other.event_base_;
        socket_ = other.socket_;
        parent_ = other.parent_;
        address_family_ = other.address_family_;

        DetachEventBase();
        AttachEventBase(other.event_base_);
        ChangeHandlerFD(other.socket_);
      }
      return *this;
    }

    virtual void HandlerReady(uint16_t events) noexcept {
      parent_->HandlerReady(events, socket_, address_family_);
    }

    EventBase* event_base_;
    int socket_;
    AsyncServerSocket* parent_;
    sa_family_t address_family_;
  };

  EventBase* event_base_;
  std::vector<ServerEventHandler> sockets_;
  std::vector<int> pending_close_sockets_;
  bool accepting_;
  uint32_t max_accept_at_once_;
  uint32_t max_num_msgs_in_queue_;
  double accept_rate_adjuest_speed_;
  double accept_rate_;
  std::chrono::time_point<std::chrono::steady_clock> last_accept_timestamp_;
  uint64_t num_dropped_connections_;
  uint32_t callback_index_;
  BackoffTimeout *backoff_timeout_;
  std::vector<CallbackInfo> callbacks_;
  bool keep_alive_enabled_;
  bool reuse_port_enabled_{false};
  bool close_on_exec_;
  ShutdownSocketSet* shutdown_socket_set_;
};

} // namespace fb
#endif // FB_AYSNC_SERVER_SOCKET_H_
