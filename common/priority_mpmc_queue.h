#pragma once
#include "common/blocking_queue.h"
#include "common/semaphore.h"
#include "common/mpmc_queue.h"

namespace fb {

template<class T>
class PriorityLifoSemMPMCQueue : public BlockingQueue<T> {
 public:
  explicit PriorityLifoSemMPMCQueue(uint32_t num_priorities,
		                    size_t capacity) {
    CHECK(num_priorities > 0);
    queues_.reserve(num_priorities);
    for (uint32_t i=0; i < num_priorities; ++i) {
      queues_.push_back(MPMCQueue<T>(capacity));
    }
  }

  uint32_t GetNumPriorities() override {
    return queues_.size();
  }

  void Add(T item) override {
    AddWithPriority(std::move(item), 0);
  }
  void AddWithPriority(T item, uint32_t priority) override {
    CHECK(priority < queues_.size());
    if (!queues_[priority].write(std::move(item))) {
      throw std::runtime_error("LifoSemMPMCQueue full, can't add item");
    }
    sem_.Post();
  }

  T Take() override {
    T item;
    while (1) {
      for (auto it = queues_.rbegin(); it != queues_.rend(); ++it) {
        if (it->read(item)) {
	  return item;
	}
      }
      sem_.Wait();
    }
  }

  size_t Size() override {
    size_t size = 0;
    for (auto& q : queues_) {
      size += q.size();
    }
    return size;
  }

 private:
  Semaphore sem_;
  std::vector<MPMCQueue<T>> queues_;
};

} // namespace fb
