//#include "fb/async_socket.h"
#include "fb/event_base.h"
//#include "fb/async_socket.h"
#include "net/socket_address.h"
#include "fb/io_buffer.h"
#include "fb/async_socket.h"

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

#if 0
class AsyncSocket::WriteRequest {
 public:
  static WriteRequest* NewRequest(WriteCallback* callback,
                                  const iovec* ops,
                                  uint32_t op_count,
                                  unique_ptr<IOBuffer>&& io_buffer,
                                  WriteFlags flags) {
    assert(op_count > 0);
    void* buf = malloc(sizeof(WriteRequest) + (op_count * sizeof(struct iovec));

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
      io_handle_(this, nullptr) {
  Init();
}

AsyncSocket::AsyncSocket(EventBase* evb)
      : event_base_(evb),
        write_timeout_(this, evb),
        io_handle_(this, evb) {
  Init();
}

#endif
} // namespace fb
