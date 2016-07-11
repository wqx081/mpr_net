// Changes(fbarchard): Ported to C++ and Google style guide.
// Made context first parameter in MD5Final for consistency with Sha1.
// Changes(hellner): added rtc namespace
// Changes(pbos): Reverted types back to uint32(8)_t with _t suffix.

#ifndef WEBRTC_BASE_MD5_H_
#define WEBRTC_BASE_MD5_H_

#include <stdint.h>
#include <stdlib.h>

namespace net {

struct MD5Context {
  uint32_t buf[4];
  uint32_t bits[2];
  uint32_t in[16];
};

void MD5Init(MD5Context* context);
void MD5Update(MD5Context* context, const uint8_t* data, size_t len);
void MD5Final(MD5Context* context, uint8_t digest[16]);
void MD5Transform(uint32_t buf[4], const uint32_t in[16]);

}  // namespace rtc

#endif  // WEBRTC_BASE_MD5_H_
