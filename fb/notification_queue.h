#ifndef FB_NOTIFICATION_QUEUE_H_
#define FB_NOTIFICATION_QUEUE_H_

#include "base/macros.h"

#include "fb/event_base.h"
#include "fb/event_handler.h"
#include "fb/request.h"
#include "fb/scope_guard.h"
#include "fb/spin_lock.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/eventfd.h>

#include <sys/syscall.h>
#include <glog/logging.h>
#include <deque>

// GLIBC_2_9
#ifndef __NR_eventfd2
#define __NR_eventfd2 290
#endif

namespace fb {

enum {
  EFD_SEMAPHORE = 1,
#define EFD_SEMAPHORE EFD_SEMAPHORE
  EFD_CLOEXEC = 02000000,
#define EFD_CLOEXEC EFD_CLOEXEC
  EFD_NONBLOCK = 04000
#define EFD_NONBLOCK EFD_NONBLOCK
};

#define eventfd(initval, flags) syscall(__NR_eventfd2, (initval), (flags))

template<typename MESSAGE>
class NotificationQueue {
 public:
  class Consumer : private EventHandler {
   public:
    enum : uint16_t { kDefaultMaxReadAtOnce = 10 };

    Consumer() : queue_(nullptr),
	             destroyed_flag_ptr_(nullptr),
		         max_read_at_once_(kDefaultMaxReadAtOnce) {}
    virtual ~Consumer();

    virtual void MessageAvailable(MESSAGE&& message) = 0;

    void StartConsuming(EventBase* event_base,
		                NotificationQueue* queue) {
      Init(event_base, queue);
      RegisterHandler(READ | PERSIST);
    }

    void StartConsumingInternal(EventBase* event_base,
		                NotificationQueue* queue) {
      Init(event_base, queue);
      RegisterInternalHandler(READ | PERSIST);
    }

    void StopConsuming();
    bool ConsumeUntilDrained() noexcept;

    NotificationQueue* GetCurrentQueue() const {
      return queue_;
    }

    void SetMaxReadAtOnce(uint32_t max_at_once) {
      max_read_at_once_ = max_at_once;
    }

    uint32_t GetMaxReadAtOnce() const {
      return max_read_at_once_;
    }

    EventBase* GetEventBase() {
      return base_;
    }

    virtual void HandlerReady(uint16_t events) noexcept;

   private:
    void ConsumeMessages(bool is_drain) noexcept;

    void SetActive(bool active, bool shuld_lock=false) {
      if (!queue_) {
        active_ = active;
        return;
      }
      if (shuld_lock) {
        queue_->spin_lock_.lock();
      }
      if (!active_ && active) {
        ++queue_->num_active_consumers_;
      } else if (active_ && !active) {
        --queue_->num_active_consumers_;
      }
      active_ = active;
      if (shuld_lock) {
        queue_->spin_lock_.unlock();
      }
    }

    void Init(EventBase* event_base, NotificationQueue* queue);

    NotificationQueue* queue_;
    bool* destroyed_flag_ptr_;
    uint32_t max_read_at_once_;
    EventBase* base_;
    bool active_{false};
  };	  

  enum class FdType {
    PIPE,
    EVENTFD, 
  };

  explicit NotificationQueue(uint32_t max_size = 0,
		             FdType fd_type = FdType::EVENTFD)
      : eventfd_(-1),
	pipe_fds_{-1, -1},
	advisory_max_queue_size_(max_size),
	pid_(getpid()),
	queue_() {

    RequestContext::GetStaticContext();

    if (fd_type == FdType::EVENTFD) {
      eventfd_ = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
      if (eventfd_ == -1) {
        if (errno == ENOSYS || errno == EINVAL) {
          // eventfd not availalble
          LOG(ERROR) << "failed to create eventfd for NotificationQueue: "
                     << errno << ", falling back to pipe mode (is your kernel "
                     << "> 2.6.30?)";
          fd_type = FdType::PIPE;
        } else {
          // some other error
          LOG(ERROR) << "Failed to create eventfd for " "NotificationQueue";
        }
      }
    }

    if (fd_type == FdType::PIPE) {
      if (pipe(pipe_fds_)) {
        LOG(ERROR) << "Failed to create pipe for NotificationQueue";
      }

      try {
	    if (fcntl(pipe_fds_[0], F_SETFL, O_RDONLY | O_NONBLOCK) != 0) {
          LOG(ERROR) << "Failed to put NotificationQueue pipe read endpoint into non-blocking mode";
        }
	    if (fcntl(pipe_fds_[0], F_SETFL, O_WRONLY | O_NONBLOCK) != 0) {
          LOG(ERROR) << "Failed to put NotificationQueue pipe write endpoint into non-blocking mode";
        }

      } catch (...) {
        ::close(pipe_fds_[0]);
        ::close(pipe_fds_[1]);
	    throw;
      }
    }
  }

  ~NotificationQueue() {
    if (eventfd_ >= 0) {
      ::close(eventfd_);
      eventfd_ = -1;
    }
    if (pipe_fds_[0] >= 0) {
      ::close(pipe_fds_[0]);
      pipe_fds_[0] = -1;
    }
    if (pipe_fds_[1] >= 0) {
      ::close(pipe_fds_[1]);
      pipe_fds_[1] = -1;
    }
  }

  void SetMaxQueueSize(uint32_t max) {
    advisory_max_queue_size_ = max;
  }

  void TryPutMessage(MESSAGE&& message) {
    PutMessageImpl(std::move(message), advisory_max_queue_size_);
  }

  void TryPutMessage(const MESSAGE& message) {
    PutMessageImpl(message, advisory_max_queue_size_);
  }

  bool TryPutMessageNoThrow(MESSAGE&& message) {
    return PutMessageImpl(std::move(message), advisory_max_queue_size_, false);
  }

  bool TryPutMessageNoThrow(const MESSAGE& message) {
    return PutMessageImpl(message, advisory_max_queue_size_, false);
  }

  void PutMessage(MESSAGE&& message) {
    PutMessageImpl(std::move(message), 0);
  }

  void PutMessage(const MESSAGE& message) {
    PutMessageImpl(message, 0);
  }

  template<typename INPUT_ITERATOR>
  void PutMessage(INPUT_ITERATOR first, INPUT_ITERATOR last) {
    typedef typename std::iterator_traits<INPUT_ITERATOR>::iterator_category
	    IteratorCategory;
    PutMessageImpl(first, last, IteratorCategory());
  }

  bool TryConsume(MESSAGE& result) {
    CheckPid();
    try {
      SpinLockGuard g(spin_lock_);

      if (MPR_UNLIKELY(queue_.empty())) {
        return false;
      }
      auto data = std::move(queue_.front());
      result = data.frist;
      RequestContext::SetContext(data.second);

      queue_.pop_front();
    } catch (...) {
      SignalEvent(1);
      throw;
    }

    return true;
  }

  int Size() {
    SpinLockGuard g(spin_lock_);
    return queue_.size();   
  }

  void CheckPid() const {
    CHECK_EQ(pid_, getpid());
  }

 private:
  inline bool CheckQueueSize(size_t max_size, bool throws=true) const {
    DCHECK(0 == spin_lock_.trylock());
    if (max_size > 0 && queue_.size() >= max_size) {
      if (throws) {
        LOG(ERROR) << "Unable to add message to NotificationQueue: queue is full";
      }
      return false;
    }
    return true;
  }

  inline bool CheckDraining(bool throws=true) {
    if (MPR_UNLIKELY(draining_ && throws)) {
      LOG(ERROR) << "Queue is draining, cannot add message";
    }
    return draining_;
  }

  inline void SignalEvent(size_t num_added = 1) const {
    static const uint8_t kPipeMessage[] = {
     1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };

    ssize_t bytes_written = 0;
    ssize_t bytes_expected = 0;
    if (eventfd_ >= 0) {
      uint64_t num_added64(num_added);
      bytes_expected = static_cast<ssize_t>(sizeof(num_added64));
      bytes_written = ::write(eventfd_, &num_added64, sizeof(num_added64));
    } else {
      bytes_expected = num_added;
      do {
        size_t message_size = std::min(num_added, sizeof(kPipeMessage));
	    ssize_t rc = ::write(pipe_fds_[1], kPipeMessage, message_size);
	    if (rc < 0) {
	      break;
	    }
	    num_added -= rc;
	    bytes_written += rc;
      } while (num_added > 0);
    }
    if (bytes_written != bytes_expected) {
      LOG(ERROR) << "Failed to signal NotificationQueue after write";
    }
  }

  bool TryConsumeEvent() {
    uint64_t value = 0;
    ssize_t rc = -1;
    if (eventfd_ >= 0) {
      rc = ::read(eventfd_, &value, sizeof(value));
    } else {
      uint8_t value8;
      rc = ::read(pipe_fds_[0], &value8, sizeof(value8));
      value = value8;
    }
    if (rc < 0) {
      assert(errno == EAGAIN);
      return false;
    }
    assert(value == 1);
    return true;
  }

  bool PutMessageImpl(MESSAGE&& message, 
		      size_t max_size, 
		      bool throws=true) {
    CheckPid();
    bool signal = false;
    {
      SpinLockGuard g(spin_lock_);
      if (CheckDraining(throws) || !CheckQueueSize(max_size, throws)) {
        return false;
      }

      if (num_active_consumers_ < num_consumers_) {
        signal = true;
      }
      queue_.emplace_back(std::move(message), RequestContext::SaveContext());
    }
    if (signal) {
      SignalEvent();
    }
    return true;
  }

  bool PutMessageImpl(const MESSAGE& message, 
		              size_t max_size, 
		              bool throws=true) {
    CheckPid();
    bool signal = false;
    {
      SpinLockGuard g(spin_lock_);
      if (CheckDraining(throws) || !CheckQueueSize(max_size, throws)) {
        return false;
      }
      if (num_active_consumers_ < num_consumers_) {
        signal = true;
      }
      queue_.emplace_back(message, RequestContext::SaveContext());
    }
    if (signal) {
      SignalEvent();
    }
    return true;
  }

  template<typename INPUT_ITERATOR>
  void PutMessageImpl(INPUT_ITERATOR first,
		      INPUT_ITERATOR last,
		      std::input_iterator_tag) {
    CheckPid();
    bool signal = false;
    size_t num_added = 0;
    {
      SpinLockGuard g(spin_lock_);
      CheckDraining();
      while (first != last) {
        queue_.emplace_back(*first, RequestContext::SaveContext());
	    ++first;
	    ++num_added;
      }
      if (num_active_consumers_ < num_consumers_) {
        signal = true;
      }
    }
    if (signal) {
      SignalEvent();
    }
  }

  mutable fb::SpinLock spin_lock_;
  int eventfd_;
  int pipe_fds_[2];
  uint32_t advisory_max_queue_size_;
  pid_t pid_;
  std::deque<std::pair<MESSAGE, std::shared_ptr<RequestContext>>> queue_;
  int num_consumers_{0};
  std::atomic<int> num_active_consumers_{0};
  bool draining_{false};

  DISALLOW_COPY_AND_ASSIGN(NotificationQueue);
};


////////////////
template<typename MESSAGE>
NotificationQueue<MESSAGE>::Consumer::~Consumer() {
  if (destroyed_flag_ptr_) {
    *destroyed_flag_ptr_ = true;
  }
}

template<typename MESSAGE>
void NotificationQueue<MESSAGE>::Consumer::HandlerReady(uint16_t events) noexcept {
  (void) events;
  ConsumeMessages(false);
}


template<typename MESSAGE>
void NotificationQueue<MESSAGE>::Consumer::ConsumeMessages(bool is_drain) noexcept {
  uint32_t num_processed = 0;
  bool first_run = true;
  SetActive(true);
  SCOPE_EXIT {
    SetActive(false, true); 
  };
  while (true) {
    if (!is_drain && first_run) {
      queue_->TryConsumeEvent();
      first_run = false;
    }

    queue_->spin_lock_.lock();
    bool locked = true;

    try {
      if (MPR_UNLIKELY(queue_->queue_.empty())) {
        SetActive(false);
        queue_->spin_lock_.unlock();
	    return;
      }

      auto& data = queue_->queue_.front();

      MESSAGE msg(std::move(data.first));
      auto old_ctx = RequestContext::SetContext(data.second);
      queue_->queue_.pop_front();

      bool was_empty = queue_->queue_.empty();
      if (was_empty) {
        SetActive(false);
      }

      queue_->spin_lock_.unlock();
      locked = false;

      bool callback_destroyed = false;
      DCHECK(destroyed_flag_ptr_ == nullptr);
      destroyed_flag_ptr_ = &callback_destroyed;
      MessageAvailable(std::move(msg));

      RequestContext::SetContext(old_ctx);

      if (callback_destroyed) {
        return;
      }
      destroyed_flag_ptr_ = nullptr;

      if (queue_ == nullptr) {
        return;
      }

      ++num_processed;
      if (!is_drain && max_read_at_once_ > 0 &&
	  num_processed >= max_read_at_once_) {
        queue_->SignalEvent(1);
	    return;
      }

      if (was_empty) {
        return;
      }
    } catch (const std::exception& ex) {
      if (locked) {
        queue_->spin_lock_.unlock();
	    if (!is_drain) {
	      queue_->SignalEvent(1);
	    }
      } 
      return;
    }
  }
}

template<typename MESSAGE>
void NotificationQueue<MESSAGE>::Consumer::Init(
		EventBase* event_base,
		NotificationQueue* queue) {
  //TODO(wqx):
  //assert(event_base->IsInEventBaseThread());
  assert(queue_ == nullptr);
  assert(!IsHandlerRegistered());
  queue->CheckPid();

  base_ = event_base;

  queue_ = queue;

  {
    SpinLockGuard g(queue_->spin_lock_);
    queue_->num_consumers_++;
  }
  queue_->SignalEvent();

  if (queue_->eventfd_ >= 0) {
    InitHandler(event_base, queue_->eventfd_);
  } else {
    InitHandler(event_base, queue_->pipe_fds_[0]);
  }
}

template<typename MESSAGE>
void NotificationQueue<MESSAGE>::Consumer::StopConsuming() {
  if (queue_ == nullptr) {
    assert(!IsHandlerRegistered());
    return;
  }

  {
    SpinLockGuard g(queue_->spin_lock_);
    queue_->num_consumers_--;
    SetActive(false);
  }

  assert(IsHandlerRegistered());
  UnregisterHandler();
  DetachEventBase();
  queue_ = nullptr;
}

template<typename MESSAGE>
bool NotificationQueue<MESSAGE>::Consumer::ConsumeUntilDrained() noexcept {
  {
    SpinLockGuard g(queue_->spin_lock_);
    if (queue_->draining_) {
      return false;
    }
    queue_->draining_ = true;
  }
  ConsumeMessages(true);
  {
    SpinLockGuard g(queue_->spin_lock_);
    queue_->draining_ = false;
  }
  return true;
}

} // namespace fb

#endif // FB_NOTIFICATION_QUEUE_H_
