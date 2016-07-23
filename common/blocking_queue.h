#pragma once

#include "base/macros.h"

namespace fb {

template<typename T>
class BlockingQueue {
 public:
  virtual ~BlockingQueue() {}
  virtual void Add(T item) = 0;
  virtual void AddWithPriority(T item, uint32_t priority) {
    LOG(WARNING) << "add() called on a non-priority queue";
    (void)priority;
    Add(std::move(item));
  }
  virtual uint32_t GetNumPriorities() {
    return 1;
  }
  virtual T Take() = 0;
  virtual size_t Size() = 0;
};

} // namespace fb
