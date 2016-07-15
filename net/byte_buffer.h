#ifndef NET_BYTE_BUFFER_H_
#define NET_BYTE_BUFFER_H_
#include "base/macros.h"
#include "net/buffer.h"

#include <string>

namespace net {

class ByteBuffer {
 public:
  enum ByteOder {
    ORDER_NETWORK = 0,
    ORDER_HOST,
  };

 private:
  ByteOder byte_oder_;
  
  DISALLOW_COPY_AND_ASSIGN(ByteBuffer);
};

} // namespace net
#endif // NET_BYTE_BUFFER_H_
