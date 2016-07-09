#include "net/net_helpers.h"


#include <memory>

#include <ifaddrs.h>

#include "base/byteorder.h"
#include "net/signal_thread.h"

using namespace base;
using namespace net;

namespace net {

int ResolveHostname(const std::string& hostname, int family,
                    std::vector<IPAddress>* addresses) {
  if (!addresses) {
    return -1;
  }
  addresses->clear();
  struct addrinfo* result = NULL;
  struct addrinfo hints; // = {0};
  memset(&hints, 0, sizeof(hints));

  hints.ai_family = family;
  // |family| here will almost always be AF_UNSPEC, because |family| comes from
  // AsyncResolver::addr_.family(), which comes from a SocketAddress constructed
  // with a hostname. When a SocketAddress is constructed with a hostname, its
  // family is AF_UNSPEC. However, if someday in the future we construct
  // a SocketAddress with both a hostname and a family other than AF_UNSPEC,
  // then it would be possible to get a specific family value here.

  // The behavior of AF_UNSPEC is roughly "get both ipv4 and ipv6", as
  // documented by the various operating systems:
  // Linux: http://man7.org/linux/man-pages/man3/getaddrinfo.3.html
  // Windows: https://msdn.microsoft.com/en-us/library/windows/desktop/
  // ms738520(v=vs.85).aspx
  // Mac: https://developer.apple.com/legacy/library/documentation/Darwin/
  // Reference/ManPages/man3/getaddrinfo.3.html
  // Android (source code, not documentation):
  // https://android.googlesource.com/platform/bionic/+/
  // 7e0bfb511e85834d7c6cb9631206b62f82701d60/libc/netbsd/net/getaddrinfo.c#1657
  hints.ai_flags = AI_ADDRCONFIG;
  int ret = getaddrinfo(hostname.c_str(), NULL, &hints, &result);
  if (ret != 0) {
    return ret;
  }
  struct addrinfo* cursor = result;
  for (; cursor; cursor = cursor->ai_next) {
    if (family == AF_UNSPEC || cursor->ai_family == family) {
      IPAddress ip;
      if (IPFromAddrInfo(cursor, &ip)) {
        addresses->push_back(ip);
      }
    }
  }
  freeaddrinfo(result);
  return 0;
}

// AsyncResolver
AsyncResolver::AsyncResolver() : error_(-1) {
}

AsyncResolver::~AsyncResolver() = default;

void AsyncResolver::Start(const SocketAddress& addr) {
  addr_ = addr;
  // SignalThred Start will kickoff the resolve process.
  SignalThread::Start();
}

bool AsyncResolver::GetResolvedAddress(int family, SocketAddress* addr) const {
  if (error_ != 0 || addresses_.empty())
    return false;

  *addr = addr_;
  for (size_t i = 0; i < addresses_.size(); ++i) {
    if (family == addresses_[i].family()) {
      addr->SetResolvedIP(addresses_[i]);
      return true;
    }
  }
  return false;
}

int AsyncResolver::GetError() const {
  return error_;
}

void AsyncResolver::Destroy(bool wait) {
  SignalThread::Destroy(wait);
}

void AsyncResolver::DoWork() {
  error_ = ResolveHostname(addr_.hostname().c_str(), addr_.family(),
                           &addresses_);
}

void AsyncResolver::OnWorkDone() {
  SignalDone(this);
}

const char* inet_ntop(int af, const void *src, char* dst, socklen_t size) {
  return ::inet_ntop(af, src, dst, size);
}

int inet_pton(int af, const char* src, void *dst) {
  return ::inet_pton(af, src, dst);
}

bool HasIPv6Enabled() {
  bool has_ipv6 = false;
  struct ifaddrs* ifa;
  if (getifaddrs(&ifa) < 0) {
    return false;
  }
  for (struct ifaddrs* cur = ifa; cur != nullptr; cur = cur->ifa_next) {
    if (cur->ifa_addr->sa_family == AF_INET6) {
      has_ipv6 = true;
      break;
    }
  }
  freeifaddrs(ifa);
  return has_ipv6;
}
}  // namespace rtc
