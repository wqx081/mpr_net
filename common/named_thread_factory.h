#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <pthread.h>

#include "common/thread_factory.h"

namespace fb {

class NamedThreadFactory : public ThreadFactory {
 public:
  explicit NamedThreadFactory(std::string prefix)
    : prefix_(prefix), suffix_(0) {}

  std::thread NewThread(Func&& func) override {
    auto thread = std::thread(std::move(func));
    pthread_setname_np(thread.native_handle(), 
		    (prefix_ + std::to_string(suffix_++)).substr(0, 15).c_str());
    return thread;
  }

  void SetNamePrefix(const std::string& prefix) {
    prefix_ = prefix;
  }
  std::string GetNamePrefix() const {
    return prefix_;
  }

 private:
  std::string prefix_;
  std::atomic<uint64_t> suffix_;
};

} // namespace fb
