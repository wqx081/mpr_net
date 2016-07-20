#include "fb/async_socket.h"
#include "fb/event_base.h"
#include "net/socket_address.h"
#include "fb/io_buffer.h"
#include "fb/async_transport.h"
#include "fb/async_socket_exception.h"

#include <poll.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
  
using std::string;
using std::unique_ptr;

namespace fb {

const AsyncSocket::OptionMap AsyncSocket::empty_option_map;

class AsyncSocket::WriteRequest {
 public:
  static WriteRequest* NewRequest(WriteCallback* callback,
                                  const iovec* ops,
                                  uint32_t op_count,
                                  unique_ptr<IOBuffer>&& io_buffer,
                                  WriteFlags flags) {
    assert(op_count > 0);
    void* buf = malloc(sizeof(WriteRequest) + (op_count * sizeof(struct iovec)));

    if (buf == nullptr) {
      throw std::bad_alloc();
    }                         
    return new(buf) WriteRequest(callback,
                                 ops,
                                 op_count,
                                 std::move(io_buffer),
                                 flags);
  }

  void Destroy() {
    this->~WriteRequest();
    free(this);
  }

  bool Cork() const {
    return IsSet(flags_, WriteFlags::CORK);
  }
  WriteFlags flags() const {
    return flags_;
  }
  WriteRequest* GetNext() const {
    return next_;
  }
  WriteCallback* GetCallback() const {
    return callback_;
  }
  uint32_t GetBytesWritten() const {
    return bytes_written_;
  }
  const struct iovec* GetOps() const {
    assert(op_count_ > op_index_);
    return write_ops_ + op_index_;
  }
  uint32_t GetOpCount() const {
    assert(op_count_ > op_index_);
    return op_count_ - op_index_;
  }
  
  void Consume(uint32_t whole_ops,
               uint32_t partial_bytes,
               uint32_t total_bytes_written) {
    op_index_ += whole_ops;
    assert(op_index_ < op_count_);

    if (io_buffer_) {
      for (uint32_t i = whole_ops; i != 0; --i) {
        assert(io_buffer_);
        io_buffer_ = io_buffer_->Pop();
      }
    }
    struct iovec* current_op = write_ops_ + op_index_;
    assert((partial_bytes < current_op->iov_len) || (current_op->iov_len == 0));
    current_op->iov_base = reinterpret_cast<uint8_t *>(current_op->iov_base) + partial_bytes;
    current_op->iov_len -= partial_bytes;
    bytes_written_ += total_bytes_written;
  }

  void Append(WriteRequest* next) {
    assert(next_ == nullptr);
    next_ = next;
  }

 private:
  WriteRequest(WriteCallback* callback,
               const struct iovec* ops,
               uint32_t op_count,
               unique_ptr<IOBuffer>&& io_buffer,
               WriteFlags flags)
      : next_(nullptr),
        callback_(callback),
        bytes_written_(0),
        op_count_(op_count),
        op_index_(0),
        flags_(flags),
        io_buffer_(std::move(io_buffer)) {
    memcpy(write_ops_, ops, sizeof(*ops) * op_count_);
  }

  ~WriteRequest() {}
  WriteRequest* next_;
  WriteCallback* callback_;
  uint32_t bytes_written_;
  uint32_t op_count_;
  uint32_t op_index_;
  WriteFlags flags_;
  unique_ptr<IOBuffer> io_buffer_;
  struct iovec write_ops_[];
};

AsyncSocket::AsyncSocket()
    : event_base_(nullptr),
      write_timeout_(this, nullptr),
      io_handler_(this, nullptr) {
  Init();
}

AsyncSocket::AsyncSocket(EventBase* evb)
      : event_base_(evb),
        write_timeout_(this, evb),
        io_handler_(this, evb) {
  Init();
}

AsyncSocket::AsyncSocket(EventBase* event_base,
                         const std::string& ip,
                         uint16_t port,
                         uint32_t connect_timeout)
    : AsyncSocket(event_base) {
  Connect(nullptr, ip, port, connect_timeout);
}

AsyncSocket::AsyncSocket(EventBase* event_base, int fd)
    : event_base_(event_base),
      write_timeout_(this, event_base),
      io_handler_(this, event_base, fd) {
  VLOG(5) << "new AsyncSocket(" << this << ", event_base=" << event_base << ", fd="
          << fd << ")";
  
  Init();
  fd_ = fd;
  SetCloseOnExec();
  state_ = StateEnum::ESTABLISHED;
}

void AsyncSocket::Init() {
  assert(event_base_ == nullptr || event_base_->IsInEventBaseThread());
  shutdown_flags_ = 0;
  state_ = StateEnum::UNINIT;
  event_flags_ = EventHandler::NONE;
  fd_ = -1;
  send_timeout_ = 0;
  max_reads_per_event_ = 16;
  connect_callback_ = nullptr;
  read_callback_ = nullptr;
  write_request_head_ = nullptr;
  write_request_tail_ = nullptr;
  shutdown_socket_set_ = nullptr;
  app_bytes_written_ = 0;
  app_bytes_received_ = 0;
}

AsyncSocket::~AsyncSocket() {
}

void AsyncSocket::Destroy() {
  CloseNow();
  DelayedDestruction::Destroy();
}

int AsyncSocket::DetachFd() {
  if (shutdown_socket_set_) {
    shutdown_socket_set_->Remove(fd_);
  }
  int fd = fd_;
  fd_ = -1;
  CloseNow();
  io_handler_.ChangeHandlerFD(-1);
  return fd;
}

const net::SocketAddress& AsyncSocket::AnyAddress() {
  static const net::SocketAddress any_address = net::SocketAddress("0.0.0.0", 0);
  return any_address;
}

void AsyncSocket::SetShudownSocketSet(ShutdownSocketSet* ss) {
  if (shutdown_socket_set_ == ss) {
    return;
  }
  if (shutdown_socket_set_ && fd_ != -1) {
    shutdown_socket_set_->Remove(fd_);
  }
  shutdown_socket_set_ = ss;
  if (shutdown_socket_set_ && fd_ != -1) {
    shutdown_socket_set_->Add(fd_);
  }
}

void AsyncSocket::SetCloseOnExec() {
  int rv = fcntl(fd_, F_SETFD, FD_CLOEXEC);
  if (rv != 0) {
    throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
                               WithAddress("Failed to set close-on-exec flag"),
                               errno);
  }
}

void AsyncSocket::Connect(ConnectCallback* connect_callback,
                          const net::SocketAddress& address,
                          int timeout,
                          const OptionMap& options,
                          const net::SocketAddress& bind_address) noexcept {
  DestructorGuard dg(this);
  assert(event_base_->IsInEventBaseThread());

  addr_ = address;

  if (state_ != StateEnum::UNINIT) {
    return InvalidState(connect_callback);
  }

  assert(fd_ == -1);
  state_ = StateEnum::CONNECTING;
  connect_callback_ = connect_callback;
  
  sockaddr_storage addr_storage;
  sockaddr* saddr = reinterpret_cast<sockaddr *>(&addr_storage);
  size_t addr_len = 0;
  
  try {
    fd_ = socket(address.family(), SOCK_STREAM, 0);
    if (fd_ < 0) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
          WithAddress("failed to create socket"),
          errno);
    }
    if (shutdown_socket_set_) {
      shutdown_socket_set_->Add(fd_);
    }
    io_handler_.ChangeHandlerFD(fd_);

    SetCloseOnExec();

    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags == -1) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
          WithAddress("failed to get socket flags"),
          errno);
    }
    int rv = fcntl(fd_, F_SETFL, flags | O_NONBLOCK);
    if (rv == -1) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
          WithAddress("failed to put socket in non-blocking mode"),
          errno);
    }
    if (address.family() != AF_UNIX) {
      (void)SetNoDelay(true);
    }

    // Bind socket
    if (bind_address != AnyAddress()) {
      int one = 1;
      if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one))) {
        DoClose();
        throw AsyncSocketException(AsyncSocketException::NOT_OPEN,
            "failed to setsockopt prior to bind on " + bind_address.ToString(),
            errno);
      }

      addr_len = bind_address.ToSockAddrStorage(&addr_storage);
      if (::bind(fd_, saddr, addr_len)) {
        DoClose();
        throw AsyncSocketException(AsyncSocketException::NOT_OPEN,
            "failed to bind to async socket: " + bind_address.ToString(),
            errno);
      }
    }

    for (const auto& opt: options) {
      int rv = opt.first.Apply(fd_, opt.second);
      if (rv != 0) {
        throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
            WithAddress("failed to set socket option"),
            errno);
      }
    } 

    addr_len = address.ToSockAddrStorage(&addr_storage);
    rv = ::connect(fd_, saddr, addr_len); 
    if (rv < 0) {
      if (errno == EINPROGRESS) {
        if (timeout > 0) {
          if (!write_timeout_.ScheduleTimeout(timeout)) {
            throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
                WithAddress("failed to schedule AsyncSocket connect timeout"));
          }
        }

        assert(event_flags_ == EventHandler::NONE);
        event_flags_ = EventHandler::WRITE;
        if (!io_handler_.RegisterHandler(event_flags_)) {
          throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
              WithAddress("failed to register AsyncSocket connect handler"));
        }
        return;
      } else {
        throw AsyncSocketException(AsyncSocketException::NOT_OPEN,
            "connect failed (immediately)", errno);
      }
    }
  } catch (const AsyncSocketException& ex) {
    (void)ex;
    return FailConnect(__func__, ex);
  } catch (const std::exception& ex) {
    AsyncSocketException tex(AsyncSocketException::INTERNAL_ERROR,
        WithAddress(std::string("unexpected exception: ") + ex.what()));
    return FailConnect(__func__, tex);
  }
  
  assert(read_callback_ == nullptr);
  assert(write_request_head_ == nullptr);
  state_ = StateEnum::ESTABLISHED;
  if (connect_callback) {
    connect_callback_ = nullptr;
    connect_callback->ConnectSuccess();
  }
}

void AsyncSocket::Connect(ConnectCallback* connect_callback,
                          const std::string& ip,
                          uint16_t port,
                          int timeout,
                          const OptionMap& options) noexcept {

  DestructorGuard dg(this);
  try {
    connect_callback_ = connect_callback;
    Connect(connect_callback, net::SocketAddress(ip, port), timeout, options);
  } catch (const std::exception& ex) {
    AsyncSocketException tex(AsyncSocketException::INTERNAL_ERROR,
                             ex.what());
    return FailConnect(__func__, tex);
  }
}

void AsyncSocket::CancelConnect() {
  connect_callback_ = nullptr;
  if (state_ == StateEnum::CONNECTING) {
    CloseNow();
  }
}

void AsyncSocket::SetSendTimeout(uint32_t milliseconds) {
  send_timeout_ = milliseconds;
  assert(event_base_ == nullptr || event_base_->IsInEventBaseThread());

  if ((event_flags_ & EventHandler::WRITE) &&
      (state_ != StateEnum::CONNECTING)) {
    assert(state_ == StateEnum::ESTABLISHED);
    assert((shutdown_flags_ & SHUT_WRITE) == 0);
    if (send_timeout_ > 0) {
      if (!write_timeout_.ScheduleTimeout(send_timeout_)) {
        AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                                WithAddress("Failed to reschedule send timeout in SetSendTimeout"));
        return FailWrite(__func__, ex);
      }
    } else {
      write_timeout_.CancelTimeout();
    }
  }
}

void AsyncSocket::SetReadCB(ReadCallback* callback) {
  if (callback == read_callback_) {
    return;
  }
  if (shutdown_flags_ & SHUT_READ) {
    if (callback != nullptr) {
      return InvalidState(callback);
    }
    assert((event_flags_ & EventHandler::READ) == 0);
    read_callback_ = nullptr;
    return;
  }
  assert(event_base_->IsInEventBaseThread());
  
  switch ((StateEnum)state_) {
    case StateEnum::CONNECTING:
      read_callback_ = callback;
      return;
    case StateEnum::ESTABLISHED:
      {
        read_callback_ = callback;
        uint16_t old_flags = event_flags_;
        if (read_callback_) {
          event_flags_ |= EventHandler::READ;
        } else {
          event_flags_ &= ~EventHandler::READ;
        }
        if (event_flags_ != old_flags) {
          (void)UpdateEventRegistration();
        }
        if (read_callback_) {
          CheckForImmediateRead();
        }
        return;
      }
    case StateEnum::CLOSED:
    case StateEnum::ERROR:
      assert(false);
      return InvalidState(callback);
    case StateEnum::UNINIT:
      return InvalidState(callback);
  }
  return InvalidState(callback);  
}

AsyncSocket::ReadCallback* AsyncSocket::GetReadCB() const {
  return read_callback_;
}

void AsyncSocket::Write(WriteCallback* callback,
                        const void* buf,
                        size_t bytes,
                        WriteFlags flags) {
  iovec op;
  op.iov_base = const_cast<void *>(buf);
  op.iov_len = bytes;
  WriteImpl(callback, &op, 1, std::move(std::unique_ptr<IOBuffer>()), flags);
}

void AsyncSocket::Writev(WriteCallback* callback,
                         const iovec* vec,
                         size_t count,
                         WriteFlags flags) {
  WriteImpl(callback, vec, count, std::move(std::unique_ptr<IOBuffer>()), flags);
}

void AsyncSocket::WriteChain(WriteCallback* callback,
                             std::unique_ptr<IOBuffer>&& buf,
                             WriteFlags flags) {
  size_t count = buf->CountChainElements();
  if (count <= 64) {
    iovec vec[count];
    WriteChainImpl(callback, vec, count, std::move(buf), flags);
  } else {
    iovec* vec = new iovec[count];
    WriteChainImpl(callback, vec, count, std::move(buf), flags);
    delete[] vec;
  }
}

void AsyncSocket::WriteChainImpl(WriteCallback* callback,
                                 iovec* vec,
                                 size_t count,
                                 std::unique_ptr<IOBuffer>&& buf,
                                 WriteFlags flags) {
  size_t veclen = buf->FillIov(vec, count);
  WriteImpl(callback, vec, veclen, std::move(buf), flags);
}

void AsyncSocket::WriteImpl(WriteCallback* callback,
                            const iovec* vec,
                            size_t count,
                            std::unique_ptr<IOBuffer>&& buf,
                            WriteFlags flags) {
  DestructorGuard dg(this);
  unique_ptr<IOBuffer> io_buffer(std::move(buf));
  assert(event_base_->IsInEventBaseThread());

  if (shutdown_flags_ & (SHUT_WRITE | SHUT_WRITE_PENDING)) {
    return InvalidState(callback);
  }
  
  uint32_t count_written = 0;
  uint32_t partial_written = 0;
  int bytes_written = 0;
  bool must_register = false;
  if (state_ == StateEnum::ESTABLISHED && !Connecting()) {
    if (write_request_head_ == nullptr) {
      assert(write_request_tail_ == nullptr);
      assert((event_flags_ & EventHandler::WRITE) == 0);

      bytes_written = PerformWrite(vec,
                                   count,
                                   flags,
                                   &count_written,
                                   &partial_written);
      if (bytes_written < 0) {
        AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                                WithAddress("writev failed"), errno);
        return FailWrite(__func__, callback, 0, ex);
      } else if (count_written == count) {
        if (callback) {
          callback->WriteSuccess();
        }
        return;
      }
      must_register = true;
    }
  } else if (!Connecting()) {
    return InvalidState(callback);
  }

  // Create a new WriteRequest to add to the queue
  WriteRequest* req;
  try {
    req = WriteRequest::NewRequest(callback,
                                   vec + count_written,
                                   count - count_written,
                                   std::move(io_buffer),
                                   flags);
  } catch (const std::exception& ex) {
    AsyncSocketException tex(AsyncSocketException::INTERNAL_ERROR,
                             WithAddress(std::string("failed to append new WriteRequest: ") + ex.what()));
    return FailWrite(__func__, callback, bytes_written, tex);
  }
  req->Consume(0, partial_written, bytes_written);
  if (write_request_tail_ == nullptr) {
    assert(write_request_head_ == nullptr);
    write_request_head_ = write_request_tail_ = req;
  } else {
    write_request_tail_->Append(req);
    write_request_tail_ = req;
  }
 
  if (must_register) {
    assert(state_ == StateEnum::ESTABLISHED);
    assert((event_flags_ & EventHandler::WRITE) == 0);
    if (!UpdateEventRegistration(EventHandler::WRITE, 0)) {
      assert(state_ == StateEnum::ERROR);
      return;
    }
    if (send_timeout_ > 0) {
      if (!write_timeout_.ScheduleTimeout(send_timeout_)) {
        AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                                WithAddress("failed to schedule send timeout"));
        return FailWrite(__func__, ex);
      }
    }
  }
}

void AsyncSocket::Close() {
  if ((write_request_head_ == nullptr) ||
      !(state_ == StateEnum::CONNECTING ||
        state_ == StateEnum::ESTABLISHED)) {
    CloseNow();
    return;
  }

  DestructorGuard dg(this);
  assert(event_base_->IsInEventBaseThread());

  shutdown_flags_ |= (SHUT_READ | SHUT_WRITE_PENDING);

  if (read_callback_) {
    if (!UpdateEventRegistration(0, EventHandler::READ)) {
      assert(state_ == StateEnum::ERROR);
      assert(read_callback_ == nullptr);
    } else {
      ReadCallback* callback = read_callback_;
      read_callback_ = nullptr;
      callback->ReadEOF();
    }
  }
}

void AsyncSocket::CloseNow() {
  DestructorGuard dg(this);
  assert(event_base_ == nullptr || event_base_->IsInEventBaseThread());

  switch (state_) {
    case StateEnum::ESTABLISHED:
    case StateEnum::CONNECTING: {
      shutdown_flags_ |= (SHUT_READ | SHUT_WRITE);
      state_ = StateEnum::CLOSED;

      write_timeout_.CancelTimeout();
      if (event_flags_ != EventHandler::NONE) {
        event_flags_ = EventHandler::NONE;
        if (!UpdateEventRegistration()) {
          assert(state_ == StateEnum::ERROR);
          return;
        }
      }

      if (fd_ >= 0) {
        io_handler_.ChangeHandlerFD(-1);
        DoClose();
      }

      if (connect_callback_) {
        ReadCallback* callback = read_callback_;
        read_callback_ = nullptr;
        callback->ReadEOF();
      }
      return;
    }
    case StateEnum::CLOSED:
      return;
    case StateEnum::ERROR:
      return;
    case StateEnum::UNINIT:
      assert(event_flags_ == EventHandler::NONE);
      assert(connect_callback_ == nullptr);
      assert(read_callback_ == nullptr);
      assert(write_request_head_ == nullptr);
      shutdown_flags_ |= (SHUT_READ | SHUT_WRITE);
      state_ = StateEnum::CLOSED;
      return;
  }
}

void AsyncSocket::CloseWithReset() {
  if (fd_) {
    struct linger opt_linger = {1, 0};
    if (SetSockOpt(SOL_SOCKET, SO_LINGER, &opt_linger) != 0) {
    }
  }
  CloseNow();
}

void AsyncSocket::ShutdownWrite() {
  if (write_request_head_ == nullptr) {
    ShutdownWriteNow();
    return;
  }
  assert(event_base_->IsInEventBaseThread());
  shutdown_flags_ |= SHUT_WRITE_PENDING;
}

void AsyncSocket::ShutdownWriteNow() {
  if (shutdown_flags_ & SHUT_WRITE) {
    return;
  }
  if (shutdown_flags_ & SHUT_READ) {
    CloseNow();
    return;
  }
  
  DestructorGuard dg(this);
  assert(event_base_ == nullptr || event_base_->IsInEventBaseThread());

  switch (state_) {
    case StateEnum::ESTABLISHED: {
      shutdown_flags_ |= SHUT_WRITE;

      write_timeout_.CancelTimeout();

      if (!UpdateEventRegistration(0, EventHandler::WRITE)) {
        assert(state_ == StateEnum::ERROR);
        return;
      }

      ::shutdown(fd_, SHUT_WR);
      //FailAllWrites(SocketShutdownForWritesEx);
      return;
    }
    case StateEnum::CONNECTING: {
      shutdown_flags_ |= SHUT_WRITE_PENDING;
      //FailAllWrites(SocketShutdownForWritesEx);
      return;
    }
    case StateEnum::UNINIT: 
      shutdown_flags_ |= SHUT_WRITE_PENDING;
      return;
    case StateEnum::CLOSED:
    case StateEnum::ERROR:
      assert(false); // YES
      return;
  }
}

bool AsyncSocket::Readable() const {
  if (fd_ == -1) {
    return false; 
  }
  struct pollfd fds[1];
  fds[0].fd = fd_;
  fds[0].events = POLLIN;
  fds[0].revents = 0;
  int rc = poll(fds, 1, 0);
  return rc == 1;
}

bool AsyncSocket::IsPending() const {
  return io_handler_.IsPending();
}

bool AsyncSocket::Hangup() const {
  if (fd_ == -1) {
    assert(false);
    return false;
  }
  struct pollfd fds[1];
  fds[0].fd = fd_;
  fds[0].events = POLLRDHUP | POLLHUP;
  fds[0].revents = 0;
  poll(fds, 1, 0);
  return (fds[0].revents & (POLLRDHUP | POLLHUP)) != 0;
}

bool AsyncSocket::Good() const {
  return ((state_ == StateEnum::CONNECTING ||
           state_ == StateEnum::ESTABLISHED) &&
          (shutdown_flags_ == 0) && (event_base_ != nullptr));
}

bool AsyncSocket::Error() const {
  return (state_ == StateEnum::ERROR);
}

void AsyncSocket::AttachEventBase(EventBase* event_base) {
  assert(event_base_ == nullptr);
  assert(event_base->IsInEventBaseThread());

  event_base_ = event_base;
  io_handler_.AttachEventBase(event_base);
  write_timeout_.AttachEventBase(event_base);
}

void AsyncSocket::DetachEventBase() {
  assert(event_base_ != nullptr);
  assert(event_base_->IsInEventBaseThread());

  event_base_ = nullptr;
  io_handler_.DetachEventBase();
  write_timeout_.DetachEventBase();
}

bool AsyncSocket::IsDetachable() const {
  DCHECK(event_base_ != nullptr);
  DCHECK(event_base_->IsInEventBaseThread());
  
  return !io_handler_.IsHandlerRegistered() && !write_timeout_.IsScheduled();
}

void AsyncSocket::GetLocalAddress(net::SocketAddress* address) const {
  address->SetFromLocalAddress(fd_);
}

void AsyncSocket::GetPeerAddress(net::SocketAddress* address) const {
  (void)address;
  //TODO
}

int AsyncSocket::SetNoDelay(bool no_delay) {
  if (fd_ < 0) {
    return EINVAL;
  }
  
  int value = no_delay ? 1 : 0;
  if (setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &value, sizeof(value)) != 0) {
    int errno_copy = errno;
    return errno_copy;
  }
  return 0;
}

int AsyncSocket::SetCongestionFlavor(const std::string& cname) {
#ifndef TCP_CONGESTION
#define TCP_CONGESTION 13
#endif
  if (fd_ < 0) {
    return EINVAL;
  }
  if (setsockopt(fd_, IPPROTO_TCP, TCP_CONGESTION, cname.c_str(), 
                 cname.length() + 1) != 0) {
    int errno_copy = errno;
    return errno_copy;
  }
  return 0;
}

int AsyncSocket::SetQuickAck(bool quick_ack) {
  if (fd_ < 0) {
    return EINVAL;
  }
  int value = quick_ack ? 1 : 0;
  if (setsockopt(fd_, IPPROTO_TCP, TCP_QUICKACK, &value, sizeof(value)) != 0 ) {
    int errno_copy = errno;
    return errno_copy;
  }
  return 0;
}

int AsyncSocket::SetSendBufferSize(size_t buffer_size) {
  if (fd_ < 0) {
    return EINVAL;
  }
  if (setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) != 0) {
    int errno_copy = errno;
    return errno_copy;
  }
  return 0;
}

int AsyncSocket::SetRecvBufferSize(size_t buffer_size) {
  if (fd_ < 0) {
    return EINVAL;
  }
  if (setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) != 0) {
    int errno_copy = errno;
    return errno_copy;
  }
  return 0;
}

int AsyncSocket::SetTCPProfile(int profd) {
  if (fd_ < 0) {
    return EINVAL;
  }
  if (setsockopt(fd_, SOL_SOCKET, SO_SET_NAMESPACE, &profd, sizeof(int)) != 0) {
    int errno_copy = errno;
    return errno_copy;
  }
  return 0;
}

void AsyncSocket::IoReady(uint16_t events) noexcept {
  DestructorGuard dg(this);
  assert(events & EventHandler::READ_WRITE);
  assert(event_base_->IsInEventBaseThread());

  uint16_t relevent_events = events & EventHandler::READ_WRITE;
  if (relevent_events == EventHandler::READ) {
    HandleRead();
  } else if (relevent_events == EventHandler::WRITE) {
    HandleWrite();
  } else if (relevent_events == EventHandler::READ_WRITE) {
    EventBase* event_base = event_base_;
    HandleWrite();
    if (event_base_ != event_base) {
      return;
    }
    if (read_callback_) {
      HandleRead();
    }
  } else {
    abort();
  }
}

ssize_t AsyncSocket::PerformRead(void* buf, size_t buf_len) {
  ssize_t bytes = recv(fd_, buf, buf_len, MSG_DONTWAIT);
  if (bytes < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      return READ_BLOCKING;
    } else {
      return READ_ERROR;
    }
  } else {
    app_bytes_received_ += bytes;
    return bytes;
  }
}


void AsyncSocket::HandleRead() noexcept {
  assert(state_ == StateEnum::ESTABLISHED);
  assert((shutdown_flags_ & SHUT_READ) == 0);
  assert(read_callback_ != nullptr);
  assert(event_flags_ & EventHandler::READ);

  uint16_t num_reads = 0;
  EventBase* event_base = event_base_;
  while (read_callback_ && event_base_ == event_base) {
    void* buf = nullptr;
    size_t buf_len = 0;
    try {
      read_callback_->GetReadBuffer(&buf, &buf_len);
    } catch (const AsyncSocketException& ex) {
      return FailRead(__func__, ex);
    } catch (const std::exception& ex) {
    } catch (...) {
    }
    if (buf == nullptr || buf_len == 0) {
    }

    // read
    ssize_t bytes_read = PerformRead(buf, buf_len);
    if (bytes_read > 0) {
      read_callback_->ReadDataAvailable(bytes_read);
      if (size_t(bytes_read) < buf_len) {
        return;
      }
    } else if (bytes_read == READ_BLOCKING) {
      return;
    } else if (bytes_read == READ_ERROR) {
      AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                              WithAddress("recv() failed"), errno);
      return FailRead(__func__, ex);
    } else {
      assert(bytes_read == READ_EOF);
      //EOF
      shutdown_flags_ |= SHUT_READ;
      if (!UpdateEventRegistration(0, EventHandler::READ)) {
        assert(state_ == StateEnum::ERROR);
        assert(read_callback_ == nullptr);
        return;
      }

      ReadCallback* callback = read_callback_;
      read_callback_ = nullptr;
      callback->ReadEOF();
      return;
    }
    if (max_reads_per_event_ && (++num_reads >= max_reads_per_event_)) {
      return;
    }
  }
}

void AsyncSocket::HandleWrite() noexcept {
  if (state_ == StateEnum::CONNECTING) {
    HandleConnect();
    return;
  }
  assert(state_ == StateEnum::ESTABLISHED);
  assert((shutdown_flags_ & SHUT_WRITE) == 0);
  assert(write_request_head_ != nullptr);

  EventBase* orig_event_base = event_base_;
  while (write_request_head_ != nullptr && event_base_ == orig_event_base) {
    uint32_t count_written;
    uint32_t partial_written;
    WriteFlags write_flags = write_request_head_->flags();
    if (write_request_head_->GetNext() != nullptr) {
      write_flags = write_flags | WriteFlags::CORK;
    }
    int bytes_written = PerformWrite(write_request_head_->GetOps(),
                                     write_request_head_->GetOpCount(),
                                     write_flags,
                                     &count_written,
                                     &partial_written);
    if (bytes_written < 0) {
      AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                              WithAddress("writev() failed"),
                              errno);
      return FailWrite(__func__, ex);
    } else if (count_written == write_request_head_->GetOpCount()) {
      WriteRequest* req = write_request_head_;
      write_request_head_ = req->GetNext();
      
      if (write_request_head_ == nullptr) {
        write_request_tail_ = nullptr;
        if (event_flags_ & EventHandler::WRITE) {
          if (!UpdateEventRegistration(0, EventHandler::WRITE)) {
            assert(state_ == StateEnum::ERROR);
            return;
          }
          write_timeout_.CancelTimeout();
        }
        assert(!write_timeout_.IsScheduled());
        
        if (shutdown_flags_ & SHUT_WRITE_PENDING) {
          assert(connect_callback_ == nullptr);
          shutdown_flags_ |= SHUT_WRITE;

          if (shutdown_flags_ & SHUT_READ) {
            assert(read_callback_ == nullptr);
            state_ = StateEnum::CLOSED;
            if (fd_ >= 0) {
              io_handler_.ChangeHandlerFD(-1);
              DoClose();
            }
          } else {
            ::shutdown(fd_, SHUT_WR);
          }
        }
      }
      
      WriteCallback* callback = req->GetCallback();
      req->Destroy();
      if (callback) {
        callback->WriteSuccess();
      }
    } else {
      write_request_head_->Consume(count_written, partial_written, bytes_written);
      if ((event_flags_ & EventHandler::WRITE) == 0) {
        if (!UpdateEventRegistration(EventHandler::WRITE, 0)) {
          assert(state_ == StateEnum::ERROR);
          return;
        }
      }
  
      if (send_timeout_ > 0) {
        if (!write_timeout_.ScheduleTimeout(send_timeout_)) {
        }
      }
      return;
    }
  }
}

void AsyncSocket::CheckForImmediateRead() noexcept {
}

void AsyncSocket::HandleInitialReadWrite() noexcept {

  DestructorGuard dg(this);

  if (read_callback_ && !(event_flags_ & EventHandler::READ)) {
  } else if (read_callback_ == nullptr) {
  }

  if (write_request_head_ && !(event_flags_ & EventHandler::WRITE)) {
  } else if (write_request_head_ == nullptr) {
  }
}

void AsyncSocket::HandleConnect() noexcept {
  assert(state_ == StateEnum::CONNECTING);
  assert((shutdown_flags_ & SHUT_WRITE) == 0);

  write_timeout_.CancelTimeout();

  assert(event_flags_ == EventHandler::WRITE);
  event_flags_ = EventHandler::NONE;

  int error;
  socklen_t len = sizeof(error);
  int rv = getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len);
  if (rv != 0) {
    AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                            WithAddress("error calling getsockopt() after connect"),
                            errno);
    return FailConnect(__func__, ex);
  }
  if (error != 0) {
  }
  state_ = StateEnum::ESTABLISHED;
  if ((shutdown_flags_ & SHUT_WRITE_PENDING) && write_request_head_ == nullptr) {
    assert((shutdown_flags_ & SHUT_READ) == 0);
    ::shutdown(fd_, SHUT_WR);
    shutdown_flags_ |= SHUT_WRITE;
  }
  
  EventBase* orig_event_base = event_base_;
  if (connect_callback_) {
    ConnectCallback* callback = connect_callback_;
    connect_callback_ = nullptr;
    callback->ConnectSuccess();
  }
  if (event_base_ != orig_event_base) {
    return;
  }

  HandleInitialReadWrite();
}

void AsyncSocket::TimeoutExpired() noexcept {
  DestructorGuard dg(this);
  assert(event_base_->IsInEventBaseThread());

  if (state_ == StateEnum::CONNECTING) {
    AsyncSocketException ex(AsyncSocketException::TIMED_OUT,
                            "connect timed out");
    FailConnect(__func__, ex);
  } else {
    assert(state_ == StateEnum::ESTABLISHED);
    AsyncSocketException ex(AsyncSocketException::TIMED_OUT,
                            "write timed out");
    FailWrite(__func__, ex);
  }
}

ssize_t AsyncSocket::PerformWrite(const iovec* vec,
                                  uint32_t count,
                                  WriteFlags flags,
                                  uint32_t* count_written,
                                  uint32_t* partial_written) {
  struct msghdr msg;
  msg.msg_name = nullptr;
  msg.msg_namelen = 0;
  msg.msg_iov = const_cast<iovec *>(vec);
  msg.msg_iovlen = std::min(count, (uint32_t)IOV_MAX);
  msg.msg_control = nullptr;
  msg.msg_controllen = 0;
  msg.msg_flags = 0;

  int msg_flags = MSG_DONTWAIT;

  msg_flags |= MSG_NOSIGNAL;
  if (IsSet(flags, WriteFlags::CORK)) {
    msg_flags |= MSG_MORE;
  }
  if (IsSet(flags, WriteFlags::EOR)) {
    msg_flags |= MSG_EOR;
  }
  ssize_t total_written = ::sendmsg(fd_, &msg, msg_flags);
  if (total_written < 0) {
    if (errno == EAGAIN) {
      *count_written = 0;
      *partial_written = 0;
      return 0;
    }
    *count_written = 0;
    *partial_written = 0;
    return -1;
  }

  app_bytes_written_ += total_written;
  
  uint32_t bytes_written;
  uint32_t n;
  for (bytes_written = total_written, n = 0; n < count; ++n) {
    const iovec* v = vec + n;
    if (v->iov_len > bytes_written) {
      *count_written = n;
      *partial_written = bytes_written;
      return total_written;
    }
 
    bytes_written -= v->iov_len;
  }

  assert(bytes_written == 0);
  *count_written = n;
  *partial_written = 0;
  return total_written;
}

bool AsyncSocket::UpdateEventRegistration() {
  assert(event_base_->IsInEventBaseThread());
  if (event_flags_ == EventHandler::NONE) {
    io_handler_.UnregisterHandler();
    return true;
  }
  if (!io_handler_.RegisterHandler(event_flags_ | EventHandler::PERSIST)) {
    event_flags_ = EventHandler::NONE;
    AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                            WithAddress("failed to update AsyncSocket event registration"));
    Fail("UpdateEventRegistration", ex);
    return false;
  }

  return true;
}

bool AsyncSocket::UpdateEventRegistration(uint16_t enable,
                                          uint16_t disable) {
  uint16_t old_flags = event_flags_;
  event_flags_ |= enable;
  event_flags_ &= ~disable;
  if (event_flags_ == old_flags) {
    return true;
  } else {
    return UpdateEventRegistration();
  }
}

void AsyncSocket::StartFail() {
  assert(state_ != StateEnum::ERROR);
  assert(GetDestructorGuardCount() > 0);

  state_ = StateEnum::ERROR;
  shutdown_flags_ |= (SHUT_READ | SHUT_WRITE);
  
  if (event_flags_ != EventHandler::NONE) {
    event_flags_ = EventHandler::NONE;
    io_handler_.UnregisterHandler();
  }

  write_timeout_.CancelTimeout();
  
  if (fd_ >= 0) {
    io_handler_.ChangeHandlerFD(-1);
    DoClose();
  }
}

void AsyncSocket::FinishFail() {
  assert(state_ == StateEnum::ERROR);
  assert(GetDestructorGuardCount() > 0);
  
  AsyncSocketException ex(AsyncSocketException::INTERNAL_ERROR,
                          WithAddress("socket closing after error"));
  if (connect_callback_) {
    ConnectCallback* callback = connect_callback_;
    connect_callback_ = nullptr;
    callback->ConnectError(ex);
  }
  
  FailAllWrites(ex);
  
  if (read_callback_) {
    ReadCallback* callback = read_callback_;
    read_callback_ = nullptr;
    callback->ReadError(ex);
  }
}

void AsyncSocket::Fail(const char* fn, const AsyncSocketException& ex) {
  (void)fn;
  (void)ex;
  StartFail();
  FinishFail();
}
  
void AsyncSocket::FailConnect(const char* fn, const AsyncSocketException& ex) {

  (void)fn;

  StartFail();
  if (connect_callback_ != nullptr) {
    ConnectCallback* callback = connect_callback_;
    connect_callback_ = nullptr;
    callback->ConnectError(ex);
  }
  
  FinishFail();
}
  
void AsyncSocket::FailRead(const char* fn, const AsyncSocketException& ex) {
  (void)fn;
  StartFail();
  if (read_callback_ != nullptr) {
    ReadCallback* callback = read_callback_;
    read_callback_ = nullptr;
    callback->ReadError(ex);
  }
  FinishFail();
}

void AsyncSocket::FailWrite(const char* fn, const AsyncSocketException& ex) {
  (void)fn;
  StartFail();
  if (write_request_head_!= nullptr) {
    WriteRequest* req = write_request_head_;
    write_request_head_ = req->GetNext();
    WriteCallback* callback = req->GetCallback();
    uint32_t bytesWritten = req->GetBytesWritten();
    req->Destroy();
    if (callback) {
      callback->WriteError(bytesWritten, ex);
    }
  }
  FinishFail();
}
  
void AsyncSocket::FailWrite(const char* fn, 
                            WriteCallback* callback,
                            size_t bytesWritten,
                            const AsyncSocketException& ex) {
  (void)fn;
  StartFail();
  
  if (callback != nullptr) {
    callback->WriteError(bytesWritten, ex);
  }
  
  FinishFail();
}
  
void AsyncSocket::FailAllWrites(const AsyncSocketException& ex) {
  while (write_request_head_!= nullptr) {
    WriteRequest* req = write_request_head_;
    write_request_head_= req->GetNext();
    WriteCallback* callback = req->GetCallback();
    if (callback) {
      callback->WriteError(req->GetBytesWritten(), ex);
    }
    req->Destroy();
  }
}

void AsyncSocket::InvalidState(ConnectCallback* callback) {
  VLOG(5) << "AsyncSocket(this=" << this << ", fd=" << fd_
          << "): connect() called in invalid state " << state_;
  AsyncSocketException ex(AsyncSocketException::ALREADY_OPEN,
                          "connect() called with socket in invalid state");
  if (state_ == StateEnum::CLOSED || state_ == StateEnum::ERROR) {
    if (callback) {
      callback->ConnectError(ex);
    }
  } else {
    StartFail();
    if (callback) {
      callback->ConnectError(ex);
    }
    FinishFail();
  }
}
  
void AsyncSocket::InvalidState(ReadCallback* callback) {
  VLOG(4) << "AsyncSocket(this=" << this << ", fd=" << fd_
          << "): setReadCallback(" << callback
          << ") called in invalid state " << state_;
  
  AsyncSocketException ex(AsyncSocketException::NOT_OPEN,
                          "setReadCallback() called with socket in "
                          "invalid state");
  if (state_ == StateEnum::CLOSED || state_ == StateEnum::ERROR) {
    if (callback) {
      callback->ReadError(ex);
    }
  } else {
    StartFail();
    if (callback) {
      callback->ReadError(ex);
    }
    FinishFail();
  }
}

void AsyncSocket::InvalidState(WriteCallback* callback) {
  VLOG(4) << "AsyncSocket(this=" << this << ", fd=" << fd_
          << "): write() called in invalid state " << state_;
  
  AsyncSocketException ex(AsyncSocketException::NOT_OPEN,
                          WithAddress("write() called with socket in invalid state"));
  if (state_ == StateEnum::CLOSED || state_ == StateEnum::ERROR) {
    if (callback) {
      callback->WriteError(0, ex);
    }
  } else {
    StartFail();
    if (callback) {
      callback->WriteError(0, ex);
    }
    FinishFail();
  }
}
  
void AsyncSocket::DoClose() {
  if (fd_ == -1) return;
  if (shutdown_socket_set_) {
    shutdown_socket_set_->Close(fd_);
  } else {
    ::close(fd_);
  }
  fd_ = -1;
}
  
std::ostream& operator << (std::ostream& os,
                           const AsyncSocket::StateEnum& state) {
  os << static_cast<int>(state);
  return os;
}
  
std::string AsyncSocket::WithAddress(const std::string& s) {
  net::SocketAddress peer, local;
  try {
    GetPeerAddress(&peer);
    GetLocalAddress(&local);
  } catch (const std::exception&) {
  } catch (...) {
  } 
  return s + " (peer=" + peer.ToSensitiveString() + ", local=" + local.ToSensitiveString() + ")";
} 


} // namespace fb
