#pragma once
#if 0
#include <folly/wangle/concurrent/BlockingQueue.h>
#include <folly/LifoSem.h>
#include <folly/MPMCQueue.h>
#endif
#include "common/blocking_queue.h"
#include "common/semaphore.h"
#include "common/mpmc_queue.h"

namespace fb {

template <class T>
class LifoSemMPMCQueue : public BlockingQueue<T> {
 public:
  explicit LifoSemMPMCQueue(size_t max_capacity) : queue_(max_capacity) {}

  void Add(T item) override {
    if (!queue_.write(std::move(item))) {
      throw std::runtime_error("LifoSemMPMCQueue full, can't add item");
    }
    sem_.Post();
  }

  T Take() override {
    T item;
    while (!queue_.read(item)) {
      sem_.Wait();
    }
    return item;
  }

  size_t Capacity() {
    return queue_.capacity();
  }

  size_t Size() override {
    return queue_.size();
  }

 private:
  Semaphore sem_;
  MPMCQueue<T> queue_;
};

}

