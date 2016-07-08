#include "base/task.h"
#include "base/task_queue.h"
#include "base/mutex.h"
#include "base/worker_thread.h"

#include <vector>

#include <gtest/gtest.h>


namespace base {

class ThreadPool {
 public:
  ThreadPool() : initialized_(false), thread_pool_size_(0) {
  }

  virtual ~ThreadPool() {
    LockGuard<Mutex> g(&lock_);
    queue_.Terminate();
    if (initialized_) {
      for (auto i = thread_pool_.begin(); i != thread_pool_.end(); ++i) {
        delete *i;
      }
    }
  }

  void SetThreadPoolSize(int thread_pool_size) {
    LockGuard<Mutex> g(&lock_);
    DCHECK(thread_pool_size >= 0);
    if (thread_pool_size < 1) {
      thread_pool_size = 2; // DEFAULT
    } 
    thread_pool_size_ = std::max(std::min(thread_pool_size, kMaxThreadPoolSize), 1);
  }

  void EnsureInitialized() {
    base::LockGuard<Mutex> g(&lock_);
    if (initialized_) {
      return;
    }

    initialized_ = true;
    for (int i = 0; i < thread_pool_size_; ++i) {
      thread_pool_.push_back(new WorkerThread(&queue_));
    }
  }

  void AddTask(Task* task) {
    EnsureInitialized();
    queue_.Append(task);
  }

 private:
  static const int kMaxThreadPoolSize = 8;

  Mutex lock_;
  bool initialized_;
  int thread_pool_size_;
  std::vector<WorkerThread*> thread_pool_;
  TaskQueue queue_;

  DISALLOW_COPY_AND_ASSIGN(ThreadPool);
};

const int ThreadPool::kMaxThreadPoolSize;

class PrintTask : public Task {
 public:
  virtual void Run() override {
    LOG(INFO) << "\t\t\tHello, World";
  }
};

TEST(ThreadPool, Basic) {
  ThreadPool thread_pool;
  thread_pool.SetThreadPoolSize(2);
  thread_pool.AddTask(new PrintTask);

  sleep(2);
  EXPECT_TRUE(true);
}

} // namespace base
