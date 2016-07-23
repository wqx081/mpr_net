#pragma once

#include "common/thread_pool_executor.h"

namespace fb {

class CPUThreadPoolExecutor : public ThreadPoolExecutor {
 public:
  ~CPUThreadPoolExecutor();

  struct CPUTask;

  explicit CPUThreadPoolExecutor(size_t num_threads,
      std::unique_ptr<BlockingQueue<CPUTask>> task_queue,
      std::shared_ptr<ThreadFactory> thread_factory =
          std::make_shared<NamedThreadFactory>("CPUThreadPool"));

  explicit CPUThreadPoolExecutor(size_t num_threads);

  explicit CPUThreadPoolExecutor(size_t num_threads,
      std::shared_ptr<ThreadFactory> thread_factory);

  explicit CPUThreadPoolExecutor(size_t num_threads,
      uint32_t num_priorities,
      std::shared_ptr<ThreadFactory> thread_factory = 
          std::make_shared<NamedThreadFactory>("CPUThreadPool"));

  explicit CPUThreadPoolExecutor(size_t num_threads,
      uint32_t num_priorities,
      size_t max_queue_size,
      std::shared_ptr<ThreadFactory> thread_factory = 
          std::make_shared<NamedThreadFactory>("CPUThreadPool"));

  void Add(Func func) override;
  void Add(Func func,
           std::chrono::milliseconds expiration,
	   Func expire_callback = nullptr) override;

  void Add(Func func, uint32_t priority);
  void Add(Func func, uint32_t priority, 
           std::chrono::milliseconds expiration,
	   Func expire_callback = nullptr);

  uint32_t GetNumPriorities() const;

  struct CPUTask : public ThreadPoolExecutor::Task {
    explicit CPUTask(Func&& f,
		     std::chrono::milliseconds expiration,
		     Func&& expire_callback)
      : Task(std::move(f), expiration, std::move(expire_callback)),
	poison(false) {}
    CPUTask()
      : Task(nullptr, std::chrono::milliseconds(0), nullptr),
	poison(true) {}

    CPUTask(CPUTask&& o) noexcept : Task(std::move(o)), poison(o.poison) {
    }
    CPUTask(const CPUTask&) = default;
    CPUTask& operator=(const CPUTask&) = default;
    bool poison;
  };

  static const size_t kDefaultMaxQueueSize;
  static const size_t kDefaultNumPriorities;

 protected:
  BlockingQueue<CPUTask>* GetTaskQueue();

 private:
  void ThreadRun(ThreadPtr thread) override;
  void StopThreads(size_t n) override;
  uint64_t GetPendingTaskCount() override;

  std::unique_ptr<BlockingQueue<CPUTask>> task_queue_;
  std::atomic<ssize_t> threads_to_stop_{0};

};

} // namespace fb
