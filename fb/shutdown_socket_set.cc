#include "fb/shutdown_socket_set.h"
#include <sys/socket.h>
#include <sys/types.h>

#include <chrono>
#include <thread>

#include <glog/logging.h>
#include <fcntl.h>
#include <unistd.h>


namespace {

template<typename F, typename... Args>
ssize_t WrapNoInt(F f, Args... args) {
  ssize_t r;
  do {
    r = f(args...);  
  } while (r == -1 && errno == EINTR);
  return r;
}


inline void* CheckedCalloc(size_t n, size_t size) {
  void* p = calloc(n, size);
  if (!p) {
    std::__throw_bad_alloc();
  }
  return p;
}

int CloseNoInt(int fd) {
  int r = close(fd);
  if (r == -1 && errno == EINTR) {
    return 0;
  }
  return r;
}

int Dup2NoInt(int old_fd, int new_fd) {
  return WrapNoInt(dup2, old_fd, new_fd);
}

int ShutdownNoInt(int fd, int how) {
  return WrapNoInt(shutdown, fd, how);
}


} // namespace

namespace fb {

ShutdownSocketSet::ShutdownSocketSet(size_t max_fd)
    : max_fd_(max_fd),
      data_(static_cast<std::atomic<uint8_t>*>(CheckedCalloc(max_fd, sizeof(std::atomic<uint8_t>)))),
      null_file_("/dev/null", O_RDWR) {
}

void ShutdownSocketSet::Add(int fd) {
  DCHECK_GE(fd, 0);
  if (size_t(fd) >= max_fd_) {
    return;
  }

  auto& sref = data_[fd];
  uint8_t prev_state = FREE;
  CHECK(sref.compare_exchange_strong(prev_state,
                                     IN_USE,
                                     std::memory_order_acq_rel))
    << "Invalid prev state for fd " << fd << ": " << int(prev_state);
}

void ShutdownSocketSet::Remove(int fd) {
  DCHECK_GE(fd, 0);
  if (size_t(fd) >= max_fd_) {
    return;
  }
  auto& sref = data_[fd];
  uint8_t prev_state = 0;
  
  retry_load:
    prev_state = sref.load(std::memory_order_relaxed);
  
  retry:
    switch (prev_state) {
    case IN_SHUTDOWN:
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      goto retry_load;
    case FREE:
      LOG(FATAL) << "Invalid prev state for fd " << fd << ": " << int(prev_state);
    }
  
    if (!sref.compare_exchange_weak(prev_state,
                                    FREE,
                                    std::memory_order_acq_rel)) {
      goto retry;
    }
}

int ShutdownSocketSet::Close(int fd) {
  DCHECK_GE(fd, 0);
  if (size_t(fd) >= max_fd_) {
    return CloseNoInt(fd);
  }  

  auto& sref = data_[fd];
  uint8_t prev_state = sref.load(std::memory_order_relaxed);
  uint8_t new_state = 0;

retry:
  switch (prev_state) {
    case IN_USE:
    case SHUT_DOWN:
      new_state = FREE;
      break;
    case IN_SHUTDOWN:
      new_state = MUST_CLOSE;
      break;
    default:
      LOG(FATAL) << "Invalid prev state for fd " << fd << " : " << int(prev_state);
  }  
  if (!sref.compare_exchange_weak(prev_state,
                                  new_state,
                                  std::memory_order_acq_rel)) {
    goto retry;
  }

  return new_state == FREE ? CloseNoInt(fd) : 0;
}

void ShutdownSocketSet::Shutdown(int fd, bool abortive) {
  DCHECK_GE(fd, 0);
  if (fd >= 0 && size_t(fd) >= max_fd_) {
    DoShutdown(fd, abortive);
    return;
  }
  auto& sref = data_[fd];
  uint8_t prev_state = IN_USE;
  if (!sref.compare_exchange_strong(prev_state,
                                    IN_SHUTDOWN,
                                    std::memory_order_acq_rel)) {
    return;
  }

  DoShutdown(fd, abortive);
  
  prev_state = IN_SHUTDOWN;
  if (sref.compare_exchange_strong(prev_state,
                                   SHUT_DOWN,
                                   std::memory_order_acq_rel)) {
    return;
  }
  CHECK_EQ(prev_state, MUST_CLOSE)
    << "Invalid prev state for fd " << fd << ": " << int(prev_state);

  CloseNoInt(fd);

  CHECK(sref.compare_exchange_strong(prev_state,
                                     FREE,
                                     std::memory_order_acq_rel))
    << "Invalid prev state for fd " << fd << ": " << int(prev_state);
}

void ShutdownSocketSet::ShutdownAll(bool abortive) {
  for (size_t i = 0; i < max_fd_; ++i) {
    auto& sref = data_[i];
    if (sref.load(std::memory_order_acquire) == IN_USE) {
      Shutdown(i, abortive);
    }
  }
}

void ShutdownSocketSet::DoShutdown(int fd, bool abortive) {

  ShutdownNoInt(fd, SHUT_RDWR);

  if (abortive) {
    struct linger l = {1, 0};
    if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l)) != 0) {
      return;
    }
  }
  Dup2NoInt(null_file_.fd_, fd);
}

} // namespace fb
