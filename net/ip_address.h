#ifndef NET_IPADDRESS_H_
#define NET_IPADDRESS_H_

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <string.h>
#include <string>
#include <vector>


#include "base/macros.h"
#include "base/byteorder.h"

namespace net {

enum IPv6AddressFlag {
  IPV6_ADDRESS_FLAG_NONE = 0x00,
  IPV6_ADDRESS_FLAG_TEMPORARY =  1 << 0,
  IPV6_ADDRESS_FLAG_DEPRECATED = 1 << 1,
};

// Wraps a union of in_addr and in6_addr.
class IPAddress {
 public:
  IPAddress() : family_(AF_UNSPEC) {
    ::memset(&u_, 0, sizeof(u_));
  }
  explicit IPAddress(const in_addr& ip4) : family_(AF_INET) {
    ::memset(&u_, 0, sizeof(u_));
    u_.ip4 = ip4;
  }
  explicit IPAddress(const in6_addr& ip6) : family_(AF_INET6) {
    u_.ip6 = ip6;
  }
  explicit IPAddress(uint32_t ip) : family_(AF_INET) {
    ::memset(&u_, 0, sizeof(u_));
    u_.ip4.s_addr = base::HostToNetwork32(ip);
  }
  IPAddress(const IPAddress& other) : family_(other.family_) {
    ::memcpy(&u_, &other.u_, sizeof(u_));
  }

  virtual ~IPAddress() {}

  const IPAddress& operator=(const IPAddress& other) {
    family_ = other.family_;
    ::memcpy(&u_, &other.u_, sizeof(u_));
    return *this;
  }

  bool operator==(const IPAddress& other) const;
  bool operator!=(const IPAddress& other) const;
  bool operator <(const IPAddress& other) const;
  bool operator >(const IPAddress& other) const;
  friend std::ostream& operator<<(std::ostream& os, const IPAddress& addr);

  int family() const { return family_; }
  in_addr ipv4_address() const;
  in6_addr ipv6_address() const;

  size_t Size() const;
  std::string ToString() const;
  std::string ToSensitiveString() const;
  IPAddress Normalized() const;
  IPAddress AsIPv6Address() const;
  uint32_t v4AddressAsHostOrderInteger() const;
  bool IsNil() const;

 private:
  int family_;
  union {
    in_addr ip4;
    in6_addr ip6;
  } u_;
};

// Only meaningful for IPv6
class InterfaceAddress : public IPAddress {
 public:
  InterfaceAddress() : ipv6_flags_(IPV6_ADDRESS_FLAG_NONE) {}
  
  InterfaceAddress(IPAddress ip)
      : IPAddress(ip), ipv6_flags_(IPV6_ADDRESS_FLAG_NONE) {}
  
  InterfaceAddress(IPAddress addr, int ipv6_flags)
      : IPAddress(addr), ipv6_flags_(ipv6_flags) {}
  
  InterfaceAddress(const in6_addr& ip6, int ipv6_flags)
      : IPAddress(ip6), ipv6_flags_(ipv6_flags) {}
  
  const InterfaceAddress & operator=(const InterfaceAddress& other);
  
  bool operator==(const InterfaceAddress& other) const;
  bool operator!=(const InterfaceAddress& other) const;
  
  int ipv6_flags() const { return ipv6_flags_; }
  friend std::ostream& operator<<(std::ostream& os,
                                  const InterfaceAddress& addr);
  
 private:
  int ipv6_flags_;
};

// Helpers
bool IPFromAddrInfo(struct addrinfo* info, IPAddress* out_addr);
bool IPFromString(const std::string& str, IPAddress* out_addr);
bool IPFromString(const std::string& str, int flags, InterfaceAddress* out_addr);
bool IPIsAny(const IPAddress& ip);
bool IPIsLoopback(const IPAddress& ip);
bool IPIsPrivate(const IPAddress& ip);
bool IPIsUnspec(const IPAddress& ip);
size_t HashIP(const IPAddress& ip);

// Only for IPv6
bool IPIs6Bone(const IPAddress& ip);
bool IPIs6To4(const IPAddress& ip);
bool IPIsLinkLocal(const IPAddress& ip);
bool IPIsMacBased(const IPAddress& ip);
bool IPIsSiteLocal(const IPAddress& ip);
bool IPIsTeredo(const IPAddress& ip);
bool IPIsULA(const IPAddress& ip);
bool IPIsV4Compatibility(const IPAddress& ip);
bool IPIsV4Mapped(const IPAddress& ip);


////////////
int IPAddressPrecedence(const IPAddress& ip);
IPAddress TruncateIP(const IPAddress& ip, int length);

IPAddress GetLoopbackIP(int family);
IPAddress GetAnyIP(int family);

int CountIPMaskBits(IPAddress mask);

} // namespace net
#endif // NET_IPADDRESS_H_
