#ifndef FB_SHUTDOWN_SOCKET_SET_H_
#define FB_SHUTDOWN_SOCKET_SET_H_
#include <atomic>
#include <cstdlib>
#include <memory>

#include "base/macros.h"

namespace fb {

class ShutdownSocketSet {
 public:
  class File {
   public:
    File(int fd, const std::string& name)
      : fd_(fd), name_(name) {}
    File(const std::string& name, int flags)
      : flags_(flags), name_(name) {}

    int fd_;
    int flags_;
    std::string name_;
  };
  
  explicit ShutdownSocketSet(size_t max_fd = 1 << 18);
  void Add(int fd);
  void Remove(int fd);
  int Close(int fd);
  void Shutdown(int fd, bool abortive=false);
  void ShutdownAll(bool abortive=false);

 private:
  void DoShutdown(int fd, bool abortive);

  enum State : uint8_t {
    FREE = 0,
    IN_USE,
    IN_SHUTDOWN,
    SHUT_DOWN,
    MUST_CLOSE,
  };
  
  struct Free {
    template<typename T>
    void operator()(T* ptr) const {
      ::free(ptr);
    }
  };

  const size_t max_fd_;
  std::unique_ptr<std::atomic<uint8_t>[], Free> data_;
  ShutdownSocketSet::File null_file_;

  DISALLOW_COPY_AND_ASSIGN(ShutdownSocketSet);
};

} // namespace fb
#endif // FB_SHUTDOWN_SOCKET_SET_H_
