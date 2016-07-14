#include "fb/async_socket.h"
#include "fb/event_base.h"
#include "net/socket_address.h"

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
 private:
  ~WriteRequest() {} 
  
  WriteRequest* next_;
  WriteCallback* callback_;
  uint32_t bytes_written_;
  uint32_t op_count_;
  uint32_t op_index_;
  WriteFlags flags_;
  //TODO(wqx):
  // unique_ptr<IOBuf> io_buf_;
  struct iovec write_ops_[];
};

} // namespace fb
