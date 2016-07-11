#include "net/physical_socket_server.h"

#include <assert.h>

#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/select.h>
#include <unistd.h>
#include <signal.h>

#include <algorithm>
#include <map>

#if 0
#include "webrtc/base/ARRAYSIZE.h"
#include "webrtc/base/basictypes.h"
#include "webrtc/base/byteorder.h"
#include "webrtc/base/common.h"
#include "webrtc/base/logging.h"
#include "webrtc/base/networkmonitor.h"
#include "webrtc/base/nullsocketserver.h"
#include "webrtc/base/timeutils.h"
#include "webrtc/base/winping.h"
#include "webrtc/base/win32socketinit.h"
#endif

#include "base/macros.h"
#include "base/byteorder.h"
#include "net/null_socket_server.h"
#include "base/timeutils.h"
#include "net/checks.h"
#include "net/logging.h"

#include <netinet/tcp.h>  // for TCP_NODELAY
#define IP_MTU 14 // Until this is integrated from linux/in.h to netinet/in.h
typedef void* SockOptArg;

using namespace base;
using namespace net;

int64_t GetSocketRecvTimestamp(int socket) {
  struct timeval tv_ioctl;
  int ret = ioctl(socket, SIOCGSTAMP, &tv_ioctl);
  if (ret != 0)
    return -1;
  int64_t timestamp =
      base::kNumMicrosecsPerSec * static_cast<int64_t>(tv_ioctl.tv_sec) +
      static_cast<int64_t>(tv_ioctl.tv_usec);
  return timestamp;
}

namespace net {

std::unique_ptr<SocketServer> SocketServer::CreateDefault() {
  return std::unique_ptr<SocketServer>(new PhysicalSocketServer);
}

PhysicalSocket::PhysicalSocket(PhysicalSocketServer* ss, SOCKET s)
  : ss_(ss), s_(s), enabled_events_(0), error_(0),
    state_((s == INVALID_SOCKET) ? CS_CLOSED : CS_CONNECTED),
    resolver_(nullptr) {
  if (s_ != INVALID_SOCKET) {
    enabled_events_ = DE_READ | DE_WRITE;

    int type = SOCK_STREAM;
    socklen_t len = sizeof(type);
    MPR_DCHECK(0 == getsockopt(s_, SOL_SOCKET, SO_TYPE, (SockOptArg)&type, &len));
    udp_ = (SOCK_DGRAM == type);
  }
}

PhysicalSocket::~PhysicalSocket() {
  Close();
}

bool PhysicalSocket::Create(int family, int type) {
  Close();
  s_ = ::socket(family, type, 0);
  udp_ = (SOCK_DGRAM == type);
  UpdateLastError();
  if (udp_)
    enabled_events_ = DE_READ | DE_WRITE;
  return s_ != INVALID_SOCKET;
}

SocketAddress PhysicalSocket::GetLocalAddress() const {
  sockaddr_storage addr_storage; // = {0};
  memset(&addr_storage, 0, sizeof(addr_storage));

  socklen_t addrlen = sizeof(addr_storage);
  sockaddr* addr = reinterpret_cast<sockaddr*>(&addr_storage);
  int result = ::getsockname(s_, addr, &addrlen);
  SocketAddress address;
  if (result >= 0) {
    SocketAddressFromSockAddrStorage(addr_storage, &address);
  } else {
    LOG_F(LS_WARNING) << "GetLocalAddress: unable to get local addr, socket="
                    << s_;
  }
  return address;
}

SocketAddress PhysicalSocket::GetRemoteAddress() const {
  sockaddr_storage addr_storage; // = {0};
  memset(&addr_storage, 0, sizeof(addr_storage));

  socklen_t addrlen = sizeof(addr_storage);
  sockaddr* addr = reinterpret_cast<sockaddr*>(&addr_storage);
  int result = ::getpeername(s_, addr, &addrlen);
  SocketAddress address;
  if (result >= 0) {
    SocketAddressFromSockAddrStorage(addr_storage, &address);
  } else {
    LOG_F(LS_WARNING) << "GetRemoteAddress: unable to get remote addr, socket="
                    << s_;
  }
  return address;
}

int PhysicalSocket::Bind(const SocketAddress& bind_addr) {
  sockaddr_storage addr_storage;
  size_t len = bind_addr.ToSockAddrStorage(&addr_storage);
  sockaddr* addr = reinterpret_cast<sockaddr*>(&addr_storage);
  int err = ::bind(s_, addr, static_cast<int>(len));
  UpdateLastError();
#if !defined(NDEBUG)
  if (0 == err) {
    dbg_addr_ = "Bound @ ";
    dbg_addr_.append(GetLocalAddress().ToString());
  }
#endif
  if (ss_->network_binder()) {
    int result =
        ss_->network_binder()->BindSocketToNetwork(s_, bind_addr.ipaddr());
    if (result < 0) {
      LOG_F(LS_INFO) << "Binding socket to network address "
                   << bind_addr.ipaddr().ToString() << " result " << result;
    }
  }
  return err;
}

int PhysicalSocket::Connect(const SocketAddress& addr) {
  // TODO(pthatcher): Implicit creation is required to reconnect...
  // ...but should we make it more explicit?
  if (state_ != CS_CLOSED) {
    SetError(EALREADY);
    return SOCKET_ERROR;
  }
  if (addr.IsUnresolvedIP()) {
    LOG(INFO) << "Resolving addr in PhysicalSocket::Connect";
    resolver_ = new AsyncResolver();
    resolver_->SignalDone.connect(this, &PhysicalSocket::OnResolveResult);
    resolver_->Start(addr);
    state_ = CS_CONNECTING;
    return 0;
  }

  return DoConnect(addr);
}

int PhysicalSocket::DoConnect(const SocketAddress& connect_addr) {
  if ((s_ == INVALID_SOCKET) &&
      !Create(connect_addr.family(), SOCK_STREAM)) {
    return SOCKET_ERROR;
  }
  sockaddr_storage addr_storage;
  size_t len = connect_addr.ToSockAddrStorage(&addr_storage);
  sockaddr* addr = reinterpret_cast<sockaddr*>(&addr_storage);
  int err = ::connect(s_, addr, static_cast<int>(len));
  UpdateLastError();
  if (err == 0) {
    state_ = CS_CONNECTED;
  } else if (IsBlockingError(GetError())) {
    state_ = CS_CONNECTING;
    enabled_events_ |= DE_CONNECT;
  } else {
    return SOCKET_ERROR;
  }

  enabled_events_ |= DE_READ | DE_WRITE;
  return 0;
}

int PhysicalSocket::GetError() const {
  CritScope cs(&crit_);
  return error_;
}

void PhysicalSocket::SetError(int error) {
  CritScope cs(&crit_);
  error_ = error;
}

AsyncSocket::ConnState PhysicalSocket::GetState() const {
  return state_;
}

int PhysicalSocket::GetOption(Option opt, int* value) {
  int slevel;
  int sopt;
  if (TranslateOption(opt, &slevel, &sopt) == -1)
    return -1;
  socklen_t optlen = sizeof(*value);
  int ret = ::getsockopt(s_, slevel, sopt, (SockOptArg)value, &optlen);
  if (ret != -1 && opt == OPT_DONTFRAGMENT) {
#if defined(WEBRTC_LINUX) && !defined(WEBRTC_ANDROID)
    *value = (*value != IP_PMTUDISC_DONT) ? 1 : 0;
#endif
  }
  return ret;
}

int PhysicalSocket::SetOption(Option opt, int value) {
  int slevel;
  int sopt;
  if (TranslateOption(opt, &slevel, &sopt) == -1)
    return -1;
  if (opt == OPT_DONTFRAGMENT) {
#if defined(WEBRTC_LINUX) && !defined(WEBRTC_ANDROID)
    value = (value) ? IP_PMTUDISC_DO : IP_PMTUDISC_DONT;
#endif
  }
  return ::setsockopt(s_, slevel, sopt, (SockOptArg)&value, sizeof(value));
}

int PhysicalSocket::Send(const void* pv, size_t cb) {
  int sent = DoSend(s_, reinterpret_cast<const char *>(pv),
      static_cast<int>(cb),
#if defined(WEBRTC_LINUX) && !defined(WEBRTC_ANDROID)
      // Suppress SIGPIPE. Without this, attempting to send on a socket whose
      // other end is closed will result in a SIGPIPE signal being raised to
      // our process, which by default will terminate the process, which we
      // don't want. By specifying this flag, we'll just get the error EPIPE
      // instead and can handle the error gracefully.
      MSG_NOSIGNAL
#else
      0
#endif
      );
  UpdateLastError();
  MaybeRemapSendError();
  // We have seen minidumps where this may be false.
  assert(sent <= static_cast<int>(cb));
  if ((sent > 0 && sent < static_cast<int>(cb)) ||
      (sent < 0 && IsBlockingError(GetError()))) {
    enabled_events_ |= DE_WRITE;
  }
  return sent;
}

int PhysicalSocket::SendTo(const void* buffer,
                           size_t length,
                           const SocketAddress& addr) {
  sockaddr_storage saddr;
  size_t len = addr.ToSockAddrStorage(&saddr);
  int sent = DoSendTo(
      s_, static_cast<const char *>(buffer), static_cast<int>(length),
#if defined(WEBRTC_LINUX) && !defined(WEBRTC_ANDROID)
      // Suppress SIGPIPE. See above for explanation.
      MSG_NOSIGNAL,
#else
      0,
#endif
      reinterpret_cast<sockaddr*>(&saddr), static_cast<int>(len));
  UpdateLastError();
  MaybeRemapSendError();
  // We have seen minidumps where this may be false.
  assert(sent <= static_cast<int>(length));
  if ((sent > 0 && sent < static_cast<int>(length)) ||
      (sent < 0 && IsBlockingError(GetError()))) {
    enabled_events_ |= DE_WRITE;
  }
  return sent;
}

int PhysicalSocket::Recv(void* buffer, size_t length, int64_t* timestamp) {
  int received = ::recv(s_, static_cast<char*>(buffer),
                        static_cast<int>(length), 0);
  if ((received == 0) && (length != 0)) {
    // Note: on graceful shutdown, recv can return 0.  In this case, we
    // pretend it is blocking, and then signal close, so that simplifying
    // assumptions can be made about Recv.
    LOG(WARNING) << "EOF from socket; deferring close event";
    // Must turn this back on so that the select() loop will notice the close
    // event.
    enabled_events_ |= DE_READ;
    SetError(EWOULDBLOCK);
    return SOCKET_ERROR;
  }
  if (timestamp) {
    *timestamp = GetSocketRecvTimestamp(s_);
  }
  UpdateLastError();
  int error = GetError();
  bool success = (received >= 0) || IsBlockingError(error);
  if (udp_ || success) {
    enabled_events_ |= DE_READ;
  }
  if (!success) {
    LOG(INFO) << "Error = " << error;
  }
  return received;
}

int PhysicalSocket::RecvFrom(void* buffer,
                             size_t length,
                             SocketAddress* out_addr,
                             int64_t* timestamp) {
  sockaddr_storage addr_storage;
  socklen_t addr_len = sizeof(addr_storage);
  sockaddr* addr = reinterpret_cast<sockaddr*>(&addr_storage);
  int received = ::recvfrom(s_, static_cast<char*>(buffer),
                            static_cast<int>(length), 0, addr, &addr_len);
  if (timestamp) {
    *timestamp = GetSocketRecvTimestamp(s_);
  }
  UpdateLastError();
  if ((received >= 0) && (out_addr != nullptr))
    SocketAddressFromSockAddrStorage(addr_storage, out_addr);
  int error = GetError();
  bool success = (received >= 0) || IsBlockingError(error);
  if (udp_ || success) {
    enabled_events_ |= DE_READ;
  }
  if (!success) {
    LOG(INFO) << "Error = " << error;
  }
  return received;
}

int PhysicalSocket::Listen(int backlog) {
  int err = ::listen(s_, backlog);
  UpdateLastError();
  if (err == 0) {
    state_ = CS_CONNECTING;
    enabled_events_ |= DE_ACCEPT;
#if !defined(NDEBUG)
    dbg_addr_ = "Listening @ ";
    dbg_addr_.append(GetLocalAddress().ToString());
#endif
  }
  return err;
}

AsyncSocket* PhysicalSocket::Accept(SocketAddress* out_addr) {
  // Always re-subscribe DE_ACCEPT to make sure new incoming connections will
  // trigger an event even if DoAccept returns an error here.
  enabled_events_ |= DE_ACCEPT;
  sockaddr_storage addr_storage;
  socklen_t addr_len = sizeof(addr_storage);
  sockaddr* addr = reinterpret_cast<sockaddr*>(&addr_storage);
  SOCKET s = DoAccept(s_, addr, &addr_len);
  UpdateLastError();
  if (s == INVALID_SOCKET)
    return nullptr;
  if (out_addr != nullptr)
    SocketAddressFromSockAddrStorage(addr_storage, out_addr);
  return ss_->WrapSocket(s);
}

int PhysicalSocket::Close() {
  if (s_ == INVALID_SOCKET)
    return 0;
  int err = ::closesocket(s_);
  UpdateLastError();
  s_ = INVALID_SOCKET;
  state_ = CS_CLOSED;
  enabled_events_ = 0;
  if (resolver_) {
    resolver_->Destroy(false);
    resolver_ = nullptr;
  }
  return err;
}

int PhysicalSocket::EstimateMTU(uint16_t* mtu) {
  SocketAddress addr = GetRemoteAddress();
  if (addr.IsAnyIP()) {
    SetError(ENOTCONN);
    return -1;
  }

  // Gets the path MTU.
  int value;
  socklen_t vlen = sizeof(value);
  int err = getsockopt(s_, IPPROTO_IP, IP_MTU, &value, &vlen);
  if (err < 0) {
    UpdateLastError();
    return err;
  }

  assert((0 <= value) && (value <= 65536));
  *mtu = value;
  return 0;
}

SOCKET PhysicalSocket::DoAccept(SOCKET socket,
                                sockaddr* addr,
                                socklen_t* addrlen) {
  return ::accept(socket, addr, addrlen);
}

int PhysicalSocket::DoSend(SOCKET socket, const char* buf, int len, int flags) {
  return ::send(socket, buf, len, flags);
}

int PhysicalSocket::DoSendTo(SOCKET socket,
                             const char* buf,
                             int len,
                             int flags,
                             const struct sockaddr* dest_addr,
                             socklen_t addrlen) {
  return ::sendto(socket, buf, len, flags, dest_addr, addrlen);
}

void PhysicalSocket::OnResolveResult(AsyncResolverInterface* resolver) {
  if (resolver != resolver_) {
    return;
  }

  int error = resolver_->GetError();
  if (error == 0) {
    error = DoConnect(resolver_->address());
  } else {
    Close();
  }

  if (error) {
    SetError(error);
    SignalCloseEvent(this, error);
  }
}

void PhysicalSocket::UpdateLastError() {
  SetError(LAST_SYSTEM_ERROR);
}

void PhysicalSocket::MaybeRemapSendError() {
}

int PhysicalSocket::TranslateOption(Option opt, int* slevel, int* sopt) {
  switch (opt) {
    case OPT_DONTFRAGMENT:
      *slevel = IPPROTO_IP;
      *sopt = IP_MTU_DISCOVER;
      break;
    case OPT_RCVBUF:
      *slevel = SOL_SOCKET;
      *sopt = SO_RCVBUF;
      break;
    case OPT_SNDBUF:
      *slevel = SOL_SOCKET;
      *sopt = SO_SNDBUF;
      break;
    case OPT_NODELAY:
      *slevel = IPPROTO_TCP;
      *sopt = TCP_NODELAY;
      break;
    case OPT_DSCP:
      LOG(WARNING) << "Socket::OPT_DSCP not supported.";
      return -1;
    //case OPT_RTP_SENDTIME_EXTN_ID:
    //  return -1;  // No logging is necessary as this not a OS socket option.
    default:
      assert(false);
      return -1;
  }
  return 0;
}

SocketDispatcher::SocketDispatcher(PhysicalSocketServer *ss)
  : PhysicalSocket(ss)
{
}

SocketDispatcher::SocketDispatcher(SOCKET s, PhysicalSocketServer *ss)
  : PhysicalSocket(ss, s)
{
}

SocketDispatcher::~SocketDispatcher() {
  Close();
}

bool SocketDispatcher::Initialize() {
  assert(s_ != INVALID_SOCKET);
  // Must be a non-blocking
  fcntl(s_, F_SETFL, fcntl(s_, F_GETFL, 0) | O_NONBLOCK);
  ss_->Add(this);
  return true;
}

bool SocketDispatcher::Create(int type) {
  return Create(AF_INET, type);
}

bool SocketDispatcher::Create(int family, int type) {
  // Change the socket to be non-blocking.
  if (!PhysicalSocket::Create(family, type))
    return false;

  if (!Initialize())
    return false;

  return true;
}

int SocketDispatcher::GetDescriptor() {
  return s_;
}

bool SocketDispatcher::IsDescriptorClosed() {
  // We don't have a reliable way of distinguishing end-of-stream
  // from readability.  So test on each readable call.  Is this
  // inefficient?  Probably.
  char ch;
  ssize_t res = ::recv(s_, &ch, 1, MSG_PEEK);
  if (res > 0) {
    // Data available, so not closed.
    return false;
  } else if (res == 0) {
    // EOF, so closed.
    return true;
  } else {  // error
    switch (errno) {
      // Returned if we've already closed s_.
      case EBADF:
      // Returned during ungraceful peer shutdown.
      case ECONNRESET:
        return true;
      default:
        // Assume that all other errors are just blocking errors, meaning the
        // connection is still good but we just can't read from it right now.
        // This should only happen when connecting (and at most once), because
        // in all other cases this function is only called if the file
        // descriptor is already known to be in the readable state. However,
        // it's not necessary a problem if we spuriously interpret a
        // "connection lost"-type error as a blocking error, because typically
        // the next recv() will get EOF, so we'll still eventually notice that
        // the socket is closed.
        LOG(WARNING) << "Assuming benign blocking error";
        return false;
    }
  }
}

uint32_t SocketDispatcher::GetRequestedEvents() {
  return enabled_events_;
}

void SocketDispatcher::OnPreEvent(uint32_t ff) {
  if ((ff & DE_CONNECT) != 0)
    state_ = CS_CONNECTED;

  if ((ff & DE_CLOSE) != 0)
    state_ = CS_CLOSED;
}

void SocketDispatcher::OnEvent(uint32_t ff, int err) {
  // Make sure we deliver connect/accept first. Otherwise, consumers may see
  // something like a READ followed by a CONNECT, which would be odd.
  if ((ff & DE_CONNECT) != 0) {
    enabled_events_ &= ~DE_CONNECT;
    SignalConnectEvent(this);
  }
  if ((ff & DE_ACCEPT) != 0) {
    enabled_events_ &= ~DE_ACCEPT;
    SignalReadEvent(this);
  }
  if ((ff & DE_READ) != 0) {
    enabled_events_ &= ~DE_READ;
    SignalReadEvent(this);
  }
  if ((ff & DE_WRITE) != 0) {
    enabled_events_ &= ~DE_WRITE;
    SignalWriteEvent(this);
  }
  if ((ff & DE_CLOSE) != 0) {
    // The socket is now dead to us, so stop checking it.
    enabled_events_ = 0;
    SignalCloseEvent(this, err);
  }
}

int SocketDispatcher::Close() {
  if (s_ == INVALID_SOCKET)
    return 0;

  ss_->Remove(this);
  return PhysicalSocket::Close();
}

class EventDispatcher : public Dispatcher {
 public:
  EventDispatcher(PhysicalSocketServer* ss) : ss_(ss), fSignaled_(false) {
    if (pipe(afd_) < 0)
      LOG_F(LS_ERROR) << "pipe failed";
    ss_->Add(this);
  }

  ~EventDispatcher() override {
    ss_->Remove(this);
    close(afd_[0]);
    close(afd_[1]);
  }

  virtual void Signal() {
    CritScope cs(&crit_);
    if (!fSignaled_) {
      const uint8_t b[1] = {0};
      if ((1 == write(afd_[1], b, sizeof(b)))) {
        fSignaled_ = true;
      }
    }
  }

  uint32_t GetRequestedEvents() override { return DE_READ; }

  void OnPreEvent(uint32_t ff) override {
    // It is not possible to perfectly emulate an auto-resetting event with
    // pipes.  This simulates it by resetting before the event is handled.
    (void)ff;

    CritScope cs(&crit_);
    if (fSignaled_) {
      uint8_t b[4];  // Allow for reading more than 1 byte, but expect 1.
      MPR_DCHECK(1 == read(afd_[0], b, sizeof(b)));
      fSignaled_ = false;
    }
  }

  void OnEvent(uint32_t ff, int err) override { (void)ff; (void)err; assert(false); }

  int GetDescriptor() override { return afd_[0]; }

  bool IsDescriptorClosed() override { return false; }

 private:
  PhysicalSocketServer *ss_;
  int afd_[2];
  bool fSignaled_;
  CriticalSection crit_;
};

// These two classes use the self-pipe trick to deliver POSIX signals to our
// select loop. This is the only safe, reliable, cross-platform way to do
// non-trivial things with a POSIX signal in an event-driven program (until
// proper pselect() implementations become ubiquitous).

class PosixSignalHandler {
 public:
  // POSIX only specifies 32 signals, but in principle the system might have
  // more and the programmer might choose to use them, so we size our array
  // for 128.
  static const int kNumPosixSignals = 128;

  // There is just a single global instance. (Signal handlers do not get any
  // sort of user-defined void * parameter, so they can't access anything that
  // isn't global.)
  static PosixSignalHandler* Instance() {
    //RTC_DEFINE_STATIC_LOCAL(PosixSignalHandler, instance, ());
    static PosixSignalHandler& instance = *new PosixSignalHandler();
    return &instance;
  }

  // Returns true if the given signal number is set.
  bool IsSignalSet(int signum) const {
    assert(signum < static_cast<int>(ARRAYSIZE(received_signal_)));
    if (signum < static_cast<int>(ARRAYSIZE(received_signal_))) {
      return received_signal_[signum];
    } else {
      return false;
    }
  }

  // Clears the given signal number.
  void ClearSignal(int signum) {
    assert(signum < static_cast<int>(ARRAYSIZE(received_signal_)));
    if (signum < static_cast<int>(ARRAYSIZE(received_signal_))) {
      received_signal_[signum] = false;
    }
  }

  // Returns the file descriptor to monitor for signal events.
  int GetDescriptor() const {
    return afd_[0];
  }

  // This is called directly from our real signal handler, so it must be
  // signal-handler-safe. That means it cannot assume anything about the
  // user-level state of the process, since the handler could be executed at any
  // time on any thread.
  void OnPosixSignalReceived(int signum) {
    if (signum >= static_cast<int>(ARRAYSIZE(received_signal_))) {
      // We don't have space in our array for this.
      return;
    }
    // Set a flag saying we've seen this signal.
    received_signal_[signum] = true;
    // Notify application code that we got a signal.
    const uint8_t b[1] = {0};
    if (-1 == write(afd_[1], b, sizeof(b))) {
      // Nothing we can do here. If there's an error somehow then there's
      // nothing we can safely do from a signal handler.
      // No, we can't even safely log it.
      // But, we still have to check the return value here. Otherwise,
      // GCC 4.4.1 complains ignoring return value. Even (void) doesn't help.
      return;
    }
  }

 private:
  PosixSignalHandler() {
    if (pipe(afd_) < 0) {
      LOG_F(LS_ERROR) << "pipe failed";
      return;
    }
    if (fcntl(afd_[0], F_SETFL, O_NONBLOCK) < 0) {
      LOG(WARNING) << "fcntl #1 failed";
    }
    if (fcntl(afd_[1], F_SETFL, O_NONBLOCK) < 0) {
      LOG(WARNING) << "fcntl #2 failed";
    }
    memset(const_cast<void *>(static_cast<volatile void *>(received_signal_)),
           0,
           sizeof(received_signal_));
  }

  ~PosixSignalHandler() {
    int fd1 = afd_[0];
    int fd2 = afd_[1];
    // We clobber the stored file descriptor numbers here or else in principle
    // a signal that happens to be delivered during application termination
    // could erroneously write a zero byte to an unrelated file handle in
    // OnPosixSignalReceived() if some other file happens to be opened later
    // during shutdown and happens to be given the same file descriptor number
    // as our pipe had. Unfortunately even with this precaution there is still a
    // race where that could occur if said signal happens to be handled
    // concurrently with this code and happens to have already read the value of
    // afd_[1] from memory before we clobber it, but that's unlikely.
    afd_[0] = -1;
    afd_[1] = -1;
    close(fd1);
    close(fd2);
  }

  int afd_[2];
  // These are boolean flags that will be set in our signal handler and read
  // and cleared from Wait(). There is a race involved in this, but it is
  // benign. The signal handler sets the flag before signaling the pipe, so
  // we'll never end up blocking in select() while a flag is still true.
  // However, if two of the same signal arrive close to each other then it's
  // possible that the second time the handler may set the flag while it's still
  // true, meaning that signal will be missed. But the first occurrence of it
  // will still be handled, so this isn't a problem.
  // Volatile is not necessary here for correctness, but this data _is_ volatile
  // so I've marked it as such.
  volatile uint8_t received_signal_[kNumPosixSignals];
};

class PosixSignalDispatcher : public Dispatcher {
 public:
  PosixSignalDispatcher(PhysicalSocketServer *owner) : owner_(owner) {
    owner_->Add(this);
  }

  ~PosixSignalDispatcher() override {
    owner_->Remove(this);
  }

  uint32_t GetRequestedEvents() override { return DE_READ; }

  void OnPreEvent(uint32_t ff) override {
    // Events might get grouped if signals come very fast, so we read out up to
    // 16 bytes to make sure we keep the pipe empty.
    uint8_t b[16];
    ssize_t ret = read(GetDescriptor(), b, sizeof(b));
    if (ret < 0) {
      LOG(WARNING) << "Error in read()";
    } else if (ret == 0) {
      LOG(WARNING) << "Should have read at least one byte";
    }
  }

  void OnEvent(uint32_t ff, int err) override {
    for (int signum = 0; signum < PosixSignalHandler::kNumPosixSignals;
         ++signum) {
      if (PosixSignalHandler::Instance()->IsSignalSet(signum)) {
        PosixSignalHandler::Instance()->ClearSignal(signum);
        HandlerMap::iterator i = handlers_.find(signum);
        if (i == handlers_.end()) {
          // This can happen if a signal is delivered to our process at around
          // the same time as we unset our handler for it. It is not an error
          // condition, but it's unusual enough to be worth logging.
          LOG(INFO) << "Received signal with no handler: " << signum;
        } else {
          // Otherwise, execute our handler.
          (*i->second)(signum);
        }
      }
    }
  }

  int GetDescriptor() override {
    return PosixSignalHandler::Instance()->GetDescriptor();
  }

  bool IsDescriptorClosed() override { return false; }

  void SetHandler(int signum, void (*handler)(int)) {
    handlers_[signum] = handler;
  }

  void ClearHandler(int signum) {
    handlers_.erase(signum);
  }

  bool HasHandlers() {
    return !handlers_.empty();
  }

 private:
  typedef std::map<int, void (*)(int)> HandlerMap;

  HandlerMap handlers_;
  // Our owner.
  PhysicalSocketServer *owner_;
};

class FileDispatcher: public Dispatcher, public AsyncFile {
 public:
  FileDispatcher(int fd, PhysicalSocketServer *ss) : ss_(ss), fd_(fd) {
    set_readable(true);

    ss_->Add(this);

    fcntl(fd_, F_SETFL, fcntl(fd_, F_GETFL, 0) | O_NONBLOCK);
  }

  ~FileDispatcher() override {
    ss_->Remove(this);
  }

  SocketServer* socketserver() { return ss_; }

  int GetDescriptor() override { return fd_; }

  bool IsDescriptorClosed() override { return false; }

  uint32_t GetRequestedEvents() override { return flags_; }

  void OnPreEvent(uint32_t ff) override {}

  void OnEvent(uint32_t ff, int err) override {
    if ((ff & DE_READ) != 0)
      SignalReadEvent(this);
    if ((ff & DE_WRITE) != 0)
      SignalWriteEvent(this);
    if ((ff & DE_CLOSE) != 0)
      SignalCloseEvent(this, err);
  }

  bool readable() override { return (flags_ & DE_READ) != 0; }

  void set_readable(bool value) override {
    flags_ = value ? (flags_ | DE_READ) : (flags_ & ~DE_READ);
  }

  bool writable() override { return (flags_ & DE_WRITE) != 0; }

  void set_writable(bool value) override {
    flags_ = value ? (flags_ | DE_WRITE) : (flags_ & ~DE_WRITE);
  }

 private:
  PhysicalSocketServer* ss_;
  int fd_;
  int flags_;
};

AsyncFile* PhysicalSocketServer::CreateFile(int fd) {
  return new FileDispatcher(fd, this);
}

// Sets the value of a boolean value to false when signaled.
class Signaler : public EventDispatcher {
 public:
  Signaler(PhysicalSocketServer* ss, bool* pf)
      : EventDispatcher(ss), pf_(pf) {
  }
  ~Signaler() override { }

  void OnEvent(uint32_t ff, int err) override {
    if (pf_)
      *pf_ = false;
  }

 private:
  bool *pf_;
};

PhysicalSocketServer::PhysicalSocketServer()
    : fWait_(false) {
  signal_wakeup_ = new Signaler(this, &fWait_);
}

PhysicalSocketServer::~PhysicalSocketServer() {
  signal_dispatcher_.reset();
  delete signal_wakeup_;
  assert(dispatchers_.empty());
}

void PhysicalSocketServer::WakeUp() {
  signal_wakeup_->Signal();
}

Socket* PhysicalSocketServer::CreateSocket(int type) {
  return CreateSocket(AF_INET, type);
}

Socket* PhysicalSocketServer::CreateSocket(int family, int type) {
  PhysicalSocket* socket = new PhysicalSocket(this);
  if (socket->Create(family, type)) {
    return socket;
  } else {
    delete socket;
    return nullptr;
  }
}

AsyncSocket* PhysicalSocketServer::CreateAsyncSocket(int type) {
  return CreateAsyncSocket(AF_INET, type);
}

AsyncSocket* PhysicalSocketServer::CreateAsyncSocket(int family, int type) {
  SocketDispatcher* dispatcher = new SocketDispatcher(this);
  if (dispatcher->Create(family, type)) {
    return dispatcher;
  } else {
    delete dispatcher;
    return nullptr;
  }
}

AsyncSocket* PhysicalSocketServer::WrapSocket(SOCKET s) {
  SocketDispatcher* dispatcher = new SocketDispatcher(s, this);
  if (dispatcher->Initialize()) {
    return dispatcher;
  } else {
    delete dispatcher;
    return nullptr;
  }
}

void PhysicalSocketServer::Add(Dispatcher *pdispatcher) {
  CritScope cs(&crit_);
  // Prevent duplicates. This can cause dead dispatchers to stick around.
  DispatcherList::iterator pos = std::find(dispatchers_.begin(),
                                           dispatchers_.end(),
                                           pdispatcher);
  if (pos != dispatchers_.end())
    return;
  dispatchers_.push_back(pdispatcher);
}

void PhysicalSocketServer::Remove(Dispatcher *pdispatcher) {
  CritScope cs(&crit_);
  DispatcherList::iterator pos = std::find(dispatchers_.begin(),
                                           dispatchers_.end(),
                                           pdispatcher);
  // We silently ignore duplicate calls to Add, so we should silently ignore
  // the (expected) symmetric calls to Remove. Note that this may still hide
  // a real issue, so we at least log a warning about it.
  if (pos == dispatchers_.end()) {
    LOG(WARNING) << "PhysicalSocketServer asked to remove a unknown "
                    << "dispatcher, potentially from a duplicate call to Add.";
    return;
  }
  size_t index = pos - dispatchers_.begin();
  dispatchers_.erase(pos);
  for (IteratorList::iterator it = iterators_.begin(); it != iterators_.end();
       ++it) {
    if (index < **it) {
      --**it;
    }
  }
}

bool PhysicalSocketServer::Wait(int cmsWait, bool process_io) {
  // Calculate timing information

  struct timeval *ptvWait = NULL;
  struct timeval tvWait;
  struct timeval tvStop;
  if (cmsWait != kForever) {
    // Calculate wait timeval
    tvWait.tv_sec = cmsWait / 1000;
    tvWait.tv_usec = (cmsWait % 1000) * 1000;
    ptvWait = &tvWait;

    // Calculate when to return in a timeval
    gettimeofday(&tvStop, NULL);
    tvStop.tv_sec += tvWait.tv_sec;
    tvStop.tv_usec += tvWait.tv_usec;
    if (tvStop.tv_usec >= 1000000) {
      tvStop.tv_usec -= 1000000;
      tvStop.tv_sec += 1;
    }
  }

  // Zero all fd_sets. Don't need to do this inside the loop since
  // select() zeros the descriptors not signaled

  fd_set fdsRead;
  FD_ZERO(&fdsRead);
  fd_set fdsWrite;
  FD_ZERO(&fdsWrite);
  // Explicitly unpoison these FDs on MemorySanitizer which doesn't handle the
  // inline assembly in FD_ZERO.
  // http://crbug.com/344505
#ifdef MEMORY_SANITIZER
  __msan_unpoison(&fdsRead, sizeof(fdsRead));
  __msan_unpoison(&fdsWrite, sizeof(fdsWrite));
#endif

  fWait_ = true;

  while (fWait_) {
    int fdmax = -1;
    {
      CritScope cr(&crit_);
      for (size_t i = 0; i < dispatchers_.size(); ++i) {
        // Query dispatchers for read and write wait state
        Dispatcher *pdispatcher = dispatchers_[i];
        assert(pdispatcher);
        if (!process_io && (pdispatcher != signal_wakeup_))
          continue;
        int fd = pdispatcher->GetDescriptor();
        if (fd > fdmax)
          fdmax = fd;

        uint32_t ff = pdispatcher->GetRequestedEvents();
        if (ff & (DE_READ | DE_ACCEPT))
          FD_SET(fd, &fdsRead);
        if (ff & (DE_WRITE | DE_CONNECT))
          FD_SET(fd, &fdsWrite);
      }
    }

    // Wait then call handlers as appropriate
    // < 0 means error
    // 0 means timeout
    // > 0 means count of descriptors ready
    int n = select(fdmax + 1, &fdsRead, &fdsWrite, NULL, ptvWait);

    // If error, return error.
    if (n < 0) {
      if (errno != EINTR) {
        LOG(LS_ERROR) << "select";
        return false;
      }
      // Else ignore the error and keep going. If this EINTR was for one of the
      // signals managed by this PhysicalSocketServer, the
      // PosixSignalDeliveryDispatcher will be in the signaled state in the next
      // iteration.
    } else if (n == 0) {
      // If timeout, return success
      return true;
    } else {
      // We have signaled descriptors
      CritScope cr(&crit_);
      for (size_t i = 0; i < dispatchers_.size(); ++i) {
        Dispatcher *pdispatcher = dispatchers_[i];
        int fd = pdispatcher->GetDescriptor();
        uint32_t ff = 0;
        int errcode = 0;

        // Reap any error code, which can be signaled through reads or writes.
        // TODO(pthatcher): Should we set errcode if getsockopt fails?
        if (FD_ISSET(fd, &fdsRead) || FD_ISSET(fd, &fdsWrite)) {
          socklen_t len = sizeof(errcode);
          ::getsockopt(fd, SOL_SOCKET, SO_ERROR, &errcode, &len);
        }

        // Check readable descriptors. If we're waiting on an accept, signal
        // that. Otherwise we're waiting for data, check to see if we're
        // readable or really closed.
        // TODO(pthatcher): Only peek at TCP descriptors.
        if (FD_ISSET(fd, &fdsRead)) {
          FD_CLR(fd, &fdsRead);
          if (pdispatcher->GetRequestedEvents() & DE_ACCEPT) {
            ff |= DE_ACCEPT;
          } else if (errcode || pdispatcher->IsDescriptorClosed()) {
            ff |= DE_CLOSE;
          } else {
            ff |= DE_READ;
          }
        }

        // Check writable descriptors. If we're waiting on a connect, detect
        // success versus failure by the reaped error code.
        if (FD_ISSET(fd, &fdsWrite)) {
          FD_CLR(fd, &fdsWrite);
          if (pdispatcher->GetRequestedEvents() & DE_CONNECT) {
            if (!errcode) {
              ff |= DE_CONNECT;
            } else {
              ff |= DE_CLOSE;
            }
          } else {
            ff |= DE_WRITE;
          }
        }

        // Tell the descriptor about the event.
        if (ff != 0) {
          pdispatcher->OnPreEvent(ff);
          pdispatcher->OnEvent(ff, errcode);
        }
      }
    }

    // Recalc the time remaining to wait. Doing it here means it doesn't get
    // calced twice the first time through the loop
    if (ptvWait) {
      ptvWait->tv_sec = 0;
      ptvWait->tv_usec = 0;
      struct timeval tvT;
      gettimeofday(&tvT, NULL);
      if ((tvStop.tv_sec > tvT.tv_sec)
          || ((tvStop.tv_sec == tvT.tv_sec)
              && (tvStop.tv_usec > tvT.tv_usec))) {
        ptvWait->tv_sec = tvStop.tv_sec - tvT.tv_sec;
        ptvWait->tv_usec = tvStop.tv_usec - tvT.tv_usec;
        if (ptvWait->tv_usec < 0) {
          assert(ptvWait->tv_sec > 0);
          ptvWait->tv_usec += 1000000;
          ptvWait->tv_sec -= 1;
        }
      }
    }
  }

  return true;
}

static void GlobalSignalHandler(int signum) {
  PosixSignalHandler::Instance()->OnPosixSignalReceived(signum);
}

bool PhysicalSocketServer::SetPosixSignalHandler(int signum,
                                                 void (*handler)(int)) {
  // If handler is SIG_IGN or SIG_DFL then clear our user-level handler,
  // otherwise set one.
  if (handler == SIG_IGN || handler == SIG_DFL) {
    if (!InstallSignal(signum, handler)) {
      return false;
    }
    if (signal_dispatcher_) {
      signal_dispatcher_->ClearHandler(signum);
      if (!signal_dispatcher_->HasHandlers()) {
        signal_dispatcher_.reset();
      }
    }
  } else {
    if (!signal_dispatcher_) {
      signal_dispatcher_.reset(new PosixSignalDispatcher(this));
    }
    signal_dispatcher_->SetHandler(signum, handler);
    if (!InstallSignal(signum, &GlobalSignalHandler)) {
      return false;
    }
  }
  return true;
}

Dispatcher* PhysicalSocketServer::signal_dispatcher() {
  return signal_dispatcher_.get();
}

bool PhysicalSocketServer::InstallSignal(int signum, void (*handler)(int)) {
  struct sigaction act;
  // It doesn't really matter what we set this mask to.
  if (sigemptyset(&act.sa_mask) != 0) {
    LOG(LS_ERROR) << "Couldn't set mask";
    return false;
  }
  act.sa_handler = handler;
  act.sa_flags = SA_RESTART;
  if (sigaction(signum, &act, NULL) != 0) {
    LOG(LS_ERROR) << "Couldn't set sigaction";
    return false;
  }
  return true;
}

}  // namespace rtc
