#include "fb/async_server_socket.h"
#include "net/socket_address.h"
#include "fb/event_base.h"
#include "fb/notification_queue.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace fb {

const uint32_t AsyncServerSocket::kDefaultMaxAcceptAtOnce;
const uint32_t AsyncServerSocket::kDefaultCallbackAcceptAtOnce;
const uint32_t AsyncServerSocket::kDefaultMaxMessagesInQueue;

int SetCloseOnExec(int fd, int value) {

  int old_flags = fcntl(fd, F_GETFD, 0);

  if (old_flags < 0) {
    return -1;
  }

  int new_flags;
  if (value != 0) {
    new_flags = old_flags | FD_CLOEXEC;
  } else {
    new_flags = old_flags & ~FD_CLOEXEC;
  }
  return fcntl(fd, F_SETFD, new_flags);
}

void AsyncServerSocket::RemoteAcceptor::Start(EventBase* event_base,
                                              uint32_t max_at_once,
                                              uint32_t max_in_queue) {
  SetMaxReadAtOnce(max_at_once);
  queue_.SetMaxQueueSize(max_in_queue);
 
  if (!event_base->RunInEventBaseThread([=]() {
       callback_->AcceptStarted();
       this->StartConsuming(event_base, &queue_);
     })) {
    throw std::invalid_argument("unable to start waiting on accept notification queue in the specified "
                                "EventBase thread");
  }
}

void AsyncServerSocket::RemoteAcceptor::Stop(EventBase* event_base,
                                             AcceptCallback* callback) {
  if (!event_base->RunInEventBaseThread([=]() {
       callback->AcceptStopped();
       delete this;
     })) {
  }
}

void AsyncServerSocket::RemoteAcceptor::MessageAvailable(QueueMessage&& msg) {
  switch (msg.type) {
    case MessageType::MSG_NEW_CONN: {
      callback_->ConnectionAccepted(msg.fd, msg.address);
    }
    case MessageType::MSG_ERROR: {
      std::runtime_error ex(msg.msg);
      callback_->AcceptError(ex);
      break;
    }
    default: {
      std::runtime_error ex("received invalid accept notification message type");
      callback_->AcceptError(ex);
    }
  }
}

class AsyncServerSocket::BackoffTimeout : public AsyncTimeout {
 public:
  BackoffTimeout(BackoffTimeout&& ) = delete;
  BackoffTimeout(AsyncServerSocket* socket)
      : AsyncTimeout(socket->GetEventBase()), socket_(socket) {}

  virtual void TimeoutExpired() noexcept {
    socket_->BackoffTimeoutExpired();
  }
 private:
  AsyncServerSocket* socket_;
};

// AsyncServerSocket
AsyncServerSocket::AsyncServerSocket(EventBase* event_base)
    : event_base_(event_base),
      accepting_(false),
      max_accept_at_once_(kDefaultMaxAcceptAtOnce),
      max_num_msgs_in_queue_(kDefaultMaxMessagesInQueue),
      accept_rate_adjuest_speed_(0),
      accept_rate_(1),
      last_accept_timestamp_(std::chrono::steady_clock::now()),
      num_dropped_connections_(0),
      callback_index_(0),
      backoff_timeout_(nullptr),
      callbacks_(),
      keep_alive_enabled_(true),
      close_on_exec_(true),
      shutdown_socket_set_(nullptr) {
}

void AsyncServerSocket::SetShutdownSocketSet(ShutdownSocketSet* ss) {
  if (shutdown_socket_set_ == ss) {
    return;
  }
  if (shutdown_socket_set_) {
    for (auto& h : sockets_) {
      shutdown_socket_set_->Remove(h.socket_);
    }
  }
  shutdown_socket_set_ = ss;
  if (shutdown_socket_set_) {
    for (auto& h : sockets_) {
      shutdown_socket_set_->Add(h.socket_);
    } 
  }
}

AsyncServerSocket::~AsyncServerSocket() {
  assert(callbacks_.empty());
}

int AsyncServerSocket::StopAccepting(int shutdown_flags) {
  int result = 0;
  assert(event_base_ == nullptr || event_base_->IsInEventBaseThread());
  
  accepting_ = false;

  for (auto& handler : sockets_) {
    handler.UnregisterHandler();
    if (shutdown_socket_set_) {
      shutdown_socket_set_->Close(handler.socket_);
    } else if (shutdown_flags >= 0) {
      result = ShutdownNoInt(handler.socket_, shutdown_flags);
      pending_close_sockets_.push_back(handler.socket_);
    } else {
      CloseNoInt(handler.socket_);
    }
  }
  sockets_.clear();

  delete backoff_timeout_;
  backoff_timeout_ = nullptr;

  std::vector<CallbackInfo> callbacks_copy;
  callbacks_.swap(callbacks_copy);
  for (std::vector<CallbackInfo>::iterator it = callbacks_copy.begin();
       it != callbacks_copy.end();
       ++it) {
    it->consumer->Stop(it->event_base, it->callback);
  }

  return result;
}

void AsyncServerSocket::Destroy() {
  StopAccepting();
  for (auto s : pending_close_sockets_) {
    CloseNoInt(s);
  }
  DelayedDestruction::Destroy();
}

void AsyncServerSocket::AttachEventBase(EventBase* event_base) {
  assert(event_base_ == nullptr);
  assert(event_base->IsInEventBaseThread());

  event_base_ = event_base;
  for (auto& handler : sockets_) {
    handler.AttachEventBase(event_base);
  }
}

void AsyncServerSocket::DetachEventBase() {
  assert(event_base_ != nullptr);
  assert(event_base_->IsInEventBaseThread());
  assert(!accepting_);

  event_base_ = nullptr;
  for (auto& handler : sockets_) {
    handler.DetachEventBase();
  }
}

void AsyncServerSocket::UseExistingSockets(const std::vector<int>& fds) {
  assert(event_base_ == nullptr ||
         event_base_->IsInEventBaseThread());
  if (sockets_.size() > 0) {
    throw std::invalid_argument("AsyncServerSocket that already has a socket");
  }
  
  for (auto fd: fds) {
    net::SocketAddress address;
    address.SetFromLocalAddress(fd);

    SetupSocket(fd);
    sockets_.emplace_back(event_base_, fd, this, address.family());
    sockets_.back().ChangeHandlerFD(fd);
  }
}

void AsyncServerSocket::UseExistingSocket(int fd) {
  UseExistingSockets({fd});
}

void AsyncServerSocket::BindSocket(int fd,
                                   const net::SocketAddress& address,
                                   bool is_existing_socket) {
  sockaddr_storage addr_storage;
  size_t addr_len = address.ToSockAddrStorage(&addr_storage);
  sockaddr* saddr = reinterpret_cast<sockaddr *>(&addr_storage);
  if (::bind(fd, saddr, addr_len) != 0) {
    if (!is_existing_socket) {
      CloseNoInt(fd);
    }
    //throw
  }
  if (!is_existing_socket) {
    sockets_.emplace_back(event_base_, fd, this, address.family());
  }
}

void AsyncServerSocket::Bind(const net::SocketAddress& address) {
  assert(event_base_ == nullptr ||
         event_base_->IsInEventBaseThread());
  int fd;
  if (sockets_.size() == 0) {
    fd = CreateSocket(address.family());
  } else if (sockets_.size() == 1) {
    if (address.family() != sockets_[0].address_family_) {
    //throw
    }
    fd = sockets_[0].socket_;
  } else {
    throw std::invalid_argument("Attempted to bind to multiple fds");
  }

  BindSocket(fd, address, !sockets_.empty());
}

void AsyncServerSocket::Bind(const std::vector<net::IPAddress>& ip_addresses,
                             uint16_t port) {
  if (ip_addresses.empty()) {
  // throw
  }
  if (!sockets_.empty()) {
  // throw
  }

  for (const net::IPAddress& ip_address : ip_addresses) {
    net::SocketAddress address(ip_address.ToString(), port);
    int fd = CreateSocket(address.family());
    BindSocket(fd, address, false);
  }
  if (sockets_.size() == 0) {
  //throw
  }
}

void AsyncServerSocket::Bind(uint16_t port) {
  struct addrinfo hints, *res, *res0;
  char sport[sizeof("65536")];

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;
  snprintf(sport, sizeof(sport), "%u", port);

  if (getaddrinfo(nullptr, sport, &hints, &res0)) {
  //throw
  }

  SCOPE_EXIT { freeaddrinfo(res0); };

  auto setup_address = [&] (struct addrinfo* res) {
    int s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s < 0 && errno == EAFNOSUPPORT) {
      return;
    }
    CHECK_GE(s, 0);

    try {
      SetupSocket(s);
    } catch (...) {
      CloseNoInt(s);
      throw;
    }

    if (res->ai_family == AF_INET6) {
      int v6_only = 1;
      CHECK(0 == setsockopt(s, 
                            IPPROTO_IPV6,
                            IPV6_V6ONLY,
                            &v6_only,
                            sizeof(v6_only)));
    }
    
    net::SocketAddress address;
    address.SetFromLocalAddress(s);

    sockets_.emplace_back(event_base_, s, this, address.family());
    
    if (::bind(s, res->ai_addr, res->ai_addrlen) != 0) {
    //throw
    }
  };

  const int kNumTries = 5;
  for (int tries = 1; true; tries++) {
    for (res = res0; res; res = res->ai_next) {
      if (res->ai_family == AF_INET6) {
        setup_address(res);
      }
    }
    if (sockets_.size() == 1 && port == 0) {
      net::SocketAddress address;
      address.SetFromLocalAddress(sockets_.back().socket_);
      snprintf(sport, sizeof(sport), "%u", address.port());
      freeaddrinfo(res0);
      CHECK_EQ(0, getaddrinfo(nullptr, sport, &hints, &res0));
    }

    try {
      for (res = res0; res; res = res->ai_next) {
        if (res->ai_family != AF_INET6) {
          setup_address(res);
        }
      }
    } catch (const std::system_error& e) {
      if (port == 0 && !sockets_.empty() && tries != kNumTries) {
        for (const auto& socket : sockets_) {
          if (socket.socket_ <= 0) {
            continue;
          } else if (shutdown_socket_set_) {
            shutdown_socket_set_->Close(socket.socket_);
          } else {
            CloseNoInt(socket.socket_);
          }
        }
        sockets_.clear();
        snprintf(sport, sizeof(sport), "%u", port);
        freeaddrinfo(res0);
        CHECK_EQ(0, getaddrinfo(nullptr, sport, &hints, &res0));
        continue;
      }
      throw;
    }
    break;
  }

  if (sockets_.size() == 0) {
  // throw
  }
}

void AsyncServerSocket::Listen(int backlog) {
  assert(event_base_ == nullptr ||
         event_base_->IsInEventBaseThread());
  
  for (auto& handler : sockets_) {
    if (::listen(handler.socket_, backlog) == -1) {
    //throw
    }
  }
}

void AsyncServerSocket::GetAddress(net::SocketAddress* address) const {
  CHECK(sockets_.size() >= 1);
  address->SetFromLocalAddress(sockets_[0].socket_);
}

std::vector<net::SocketAddress> AsyncServerSocket::GetAddresses() const {
  CHECK(sockets_.size() >= 1);
  auto vec = std::vector<net::SocketAddress>(sockets_.size());
  auto it = vec.begin();
  for (const auto& socket : sockets_) {
    (it++)->SetFromLocalAddress(socket.socket_);
  }
  return vec;
}

void AsyncServerSocket::AddAcceptCallback(AcceptCallback* callback,
                                          EventBase* event_base,
                                          uint32_t max_at_once) {
  assert(event_base_ == nullptr ||
         event_base_->IsInEventBaseThread());  
  bool run_start_accepting = accepting_ && callbacks_.empty();

  if (!event_base) {
    event_base = event_base_;
  }

  callbacks_.emplace_back(callback, event_base);

  RemoteAcceptor* acceptor = nullptr;
  try {
    acceptor = new RemoteAcceptor(callback);
    acceptor->Start(event_base, max_at_once, max_num_msgs_in_queue_);
  } catch (...) {
    callbacks_.pop_back();
    delete acceptor;
    throw;
  }
  callbacks_.back().consumer = acceptor;

  if (run_start_accepting) {
    StartAccepting();
  }
}

void AsyncServerSocket::RemoveAcceptCallback(AcceptCallback* callback,
                                             EventBase* event_base) {
  assert(event_base_ == nullptr ||
         event_base_->IsInEventBaseThread());  

  std::vector<CallbackInfo>::iterator it = callbacks_.begin();
  uint32_t n = 0;
  for (;;) {
    if (it == callbacks_.end()) {
    //throw
    }
    if (it->callback == callback &&
        (it->event_base == event_base || event_base == nullptr)) {
      break;
    }
    ++it;
    ++n;
  }

  CallbackInfo info(*it);
  callbacks_.erase(it);
  if (n < callback_index_) {
    --callback_index_;
  } else {
    if (callback_index_ >= callbacks_.size()) {
      callback_index_ = 0;
    }
  }

  info.consumer->Stop(info.event_base, info.callback);

  if (accepting_ && callbacks_.empty()) {
    for (auto& handler : sockets_) {
      handler.UnregisterHandler();
    }
  }
}

void AsyncServerSocket::StartAccepting() {
  assert(event_base_ == nullptr ||
         event_base_->IsInEventBaseThread());  

  accepting_ = true;
  if (callbacks_.empty()) {
    return;
  }

  for (auto& handler : sockets_) {
    if (!handler.RegisterHandler(EventHandler::READ |
                                 EventHandler::PERSIST)) {
      throw std::runtime_error("failed to register for accept events");
    }
  }
}

void AsyncServerSocket::PauseAccepting() {
  assert(event_base_ == nullptr || event_base_->IsInEventBaseThread());

  accepting_ = false;
  for (auto& handler : sockets_) {
    handler.UnregisterHandler();
  }
  
  if (backoff_timeout_) {
    backoff_timeout_->CancelTimeout();
  }
}
  
int AsyncServerSocket::CreateSocket(int family) {
  int fd = socket(family, SOCK_STREAM, 0);
  if (fd == -1) {
  //folly::throwSystemError(errno, "error creating async server socket");
  }
  
  try {
    SetupSocket(fd);
  } catch (...) {
    CloseNoInt(fd);
    throw;
  }
  return fd;
}

void AsyncServerSocket::SetupSocket(int fd) {
  net::SocketAddress address;
  address.SetFromLocalAddress(fd);
  auto family = address.family();
  
  if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
  //  folly::throwSystemError(errno, "failed to put socket in non-blocking mode");
  }
  
  int one = 1;
  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0) {
    LOG(ERROR) << "failed to set SO_REUSEADDR on async server socket " << errno;
  }
  
  int zero = 0;
  if (reuse_port_enabled_ &&
      setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(int)) != 0) {
      LOG(ERROR) << "failed to set SO_REUSEPORT on async server socket "
                 << strerror(errno);
//      folly::throwSystemError(errno, "failed to bind to async server socket: " +
//                              address.describe());
  }
  
  if (setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE,
                 (keep_alive_enabled_) ? &one : &zero, sizeof(int)) != 0) {
      LOG(ERROR) << "failed to set SO_KEEPALIVE on async server socket: " <<
              strerror(errno);
  }
  
  if (close_on_exec_ && (-1 == fb::SetCloseOnExec(fd, close_on_exec_))) {
      LOG(ERROR) << "failed to set FD_CLOEXEC on async server socket: " << strerror(errno);
  }         
    
  if (family != AF_UNIX) {
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) != 0) {
      LOG(ERROR) << "failed to set TCP_NODELAY on async server socket: " << strerror(errno);
    }         
  } 
  
  if (shutdown_socket_set_) {
    shutdown_socket_set_->Add(fd);
  } 
} 

void AsyncServerSocket::HandlerReady(uint16_t events,
                                    int fd,
                                    sa_family_t address_family) noexcept {
  (void)events;
  assert(!callbacks_.empty());
  DestructorGuard dg(this);

  for (uint32_t n = 0; n < max_accept_at_once_; ++n) {
    net::SocketAddress address;

    sockaddr_storage addr_storage;
    socklen_t addr_len = sizeof(addr_storage);
    sockaddr* saddr = reinterpret_cast<sockaddr*>(&addr_storage);
    sockaddr_in* in_addr = reinterpret_cast<sockaddr_in *>(&addr_storage);

    saddr->sa_family = address_family;
    if (address_family == AF_UNIX) {
      //addr_len = sizeof(struct sockaddr_un);
      throw std::runtime_error("UNIX not supported yet");
    }
   
    int client_socket = accept4(fd, saddr, &addr_len, SOCK_NONBLOCK);

    address.FromSockAddr(*in_addr);
    
    std::chrono::time_point<std::chrono::steady_clock> now_ms = std::chrono::steady_clock::now();
    int64_t time_since_last_accept = std::max(int64_t(0),
                                              now_ms.time_since_epoch().count() -
                                              last_accept_timestamp_.time_since_epoch().count());
    last_accept_timestamp_ = now_ms;
    if (accept_rate_ < 1) {
      accept_rate_ *= 1 + accept_rate_adjuest_speed_ * time_since_last_accept;
      if (accept_rate_ >= 1) {
        accept_rate_ = 1;
      } else if (rand() > accept_rate_ * RAND_MAX) {
        ++num_dropped_connections_;
        if (client_socket >= 0) {
          CloseNoInt(client_socket);
        }
        continue;
      }
    }

    if (client_socket < 0) {
      if (errno == EAGAIN) {
        return;
      } else if (errno == EMFILE || errno == ENFILE) {
        LOG(ERROR) << "accept failed: out of file descriptors; entering accept back-off state";

        EnterBackoff();

        DispatchError("accept() failed", errno);
      } else {
        DispatchError("accept() failed", errno);
      }
      return;
    }

    if (fcntl(client_socket, F_SETFL, 0, O_NONBLOCK) != 0) {
      CloseNoInt(client_socket);
      DispatchError("failed to set accepted socket to non-blocking mode", errno); 
      return;
    }

    DispatchSocket(client_socket, std::move(address));

    if (!accepting_ || callbacks_.empty()) {
      break;
    }
  } 
}

void AsyncServerSocket::DispatchSocket(int socket,
                                       net::SocketAddress&& address) {
  uint32_t starting_index = callback_index_;
  
  CallbackInfo *info = NextCallback();
  if (info->event_base == nullptr) {
    info->callback->ConnectionAccepted(socket, address);
    return;
  }
  
  QueueMessage msg;
  msg.type = MessageType::MSG_NEW_CONN;
  msg.address = std::move(address);
  msg.fd = socket;
  
  while (true) {
    if (info->consumer->GetQueue()->TryPutMessageNoThrow(std::move(msg))) {
      return;
    }
  
    ++num_dropped_connections_;
    if (accept_rate_adjuest_speed_> 0) {
      static const double kAcceptRateDecreaseSpeed = 0.1;
      accept_rate_ *= 1 - kAcceptRateDecreaseSpeed;
    }
  
    if (callback_index_ == starting_index) {
        LOG(ERROR) << "failed to dispatch newly accepted socket:"
                   << " all accept callback queues are full";
        CloseNoInt(socket);
        return;
    }
    info = NextCallback();
  }
}

  
void AsyncServerSocket::DispatchError(const char *msgstr, int errnoValue) {
  uint32_t starting_index = callback_index_;
  CallbackInfo *info = NextCallback();
  
  QueueMessage msg;
  msg.type = MessageType::MSG_ERROR;
  msg.error = errnoValue;
  msg.msg = std::move(msgstr);
  
  while (true) {
    if (info->event_base == nullptr) {
      std::runtime_error ex(
          std::string(msgstr) +  std::to_string(errnoValue));
        info->callback->AcceptError(ex);
        return;
    } 
      
    if (info->consumer->GetQueue()->TryPutMessageNoThrow(std::move(msg))) {
      return;
    } 
      
    if (callback_index_ == starting_index) {
        LOG(ERROR) << "failed to dispatch accept error: all accept callback "
          "queues are full: error msg:  " <<
          msg.msg.c_str() << errnoValue;
      return;
    } 
    info = NextCallback();
  } 
} 

void AsyncServerSocket::EnterBackoff() {
  if (backoff_timeout_ == nullptr) {
      try {
        backoff_timeout_ = new BackoffTimeout(this);
      } catch (const std::bad_alloc& ex) {
        LOG(ERROR) << "failed to allocate AsyncServerSocket backoff"
                   << " timer; unable to temporarly pause accepting";
        return;    
      } 
    } 
  
  const uint32_t timeoutMS = 1000;
  if (!backoff_timeout_->ScheduleTimeout(timeoutMS)) {
    LOG(ERROR) << "failed to schedule AsyncServerSocket backoff timer;"
                 << "unable to temporarly pause accepting";
    return;
  }
  
  for (auto& handler : sockets_) {
    handler.UnregisterHandler();
  }
}

void AsyncServerSocket::BackoffTimeoutExpired() {
  assert(accepting_);
  assert(event_base_ != nullptr && event_base_->IsInEventBaseThread());
  
  if (callbacks_.empty()) {
    return;
  }
  
  for (auto& handler : sockets_) {
    if (!handler.RegisterHandler(EventHandler::READ | EventHandler::PERSIST)) {
      LOG(ERROR)
          << "failed to re-enable AsyncServerSocket accepts after backoff; "
          << "crashing now";
      abort();
    }
  }
}



} // namespace fb
