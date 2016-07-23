#pragma once

#include "fb/executor.h"
#include "common/semaphore.h"
#include "common/named_thread_factory.h"
#include "common/observable.h"
#include "common/baton.h"
#include "common/blocking_queue.h"
#include "fb/rw_spin_lock.h"

#include <algorithm>
#include <mutex>
#include <queue>

#include "base/macros.h"

namespace fb {

class ThreadPoolExecutor : public virtual Executor {
 public:
  ThreadPoolExecutor(size_t num_threads,
		     std::shared_ptr<ThreadFactory> thread_factory);

  ~ThreadPoolExecutor();

  virtual void Add(Func func) override = 0;
  virtual void Add(Func func,
		   std::chrono::milliseconds expiration,
		   Func expiration_callback) = 0;
  void SetThreadFactory(std::shared_ptr<ThreadFactory> thread_factory);
  std::shared_ptr<ThreadFactory> GetThreadFactory();
  size_t num_threads();
  void SetNumThreads(size_t n);

  void Stop();
  void Join();

  struct PoolStats {
    PoolStats() : thread_count(0),
	idle_thread_count(0),
	active_thread_count(0),
	pending_task_count(0),
	total_task_count(0) {
    }
    size_t thread_count;
    size_t idle_thread_count;
    size_t active_thread_count;
    uint64_t pending_task_count;
    uint64_t total_task_count;
  };

  PoolStats GetPoolStats();

  struct TaskStats {
    TaskStats(): expired(false), wait_time(0), run_time(0) {}
    bool expired;
    std::chrono::nanoseconds wait_time;
    std::chrono::nanoseconds run_time;
  };

  Subscription<TaskStats> SubscribeToTaskStats(const ObserverPtr<TaskStats>&
		                               observer) {
    return task_stats_subject_->Subscribe(observer);
  }

  class ThreadHandle {
   public:
    virtual ~ThreadHandle() = default;
  };

  class Observer {
   public:
    virtual void ThreadStarted(ThreadHandle*) = 0;
    virtual void ThreadStopped(ThreadHandle*) = 0;
    virtual void ThreadPreviouslyStarted(ThreadHandle* h) {
      ThreadStarted(h);
    }
    virtual void ThreadNotYetStopped(ThreadHandle* h) {
      ThreadStopped(h);
    }
    virtual ~Observer() = default;
  };

  void AddObserver(std::shared_ptr<Observer>);
  void RemoveObserver(std::shared_ptr<Observer>);

 protected:
  void AddThreads(size_t n);
  void RemoveThreads(size_t n, bool is_join);

  struct AVOID_FALSE_SHARING Thread : public ThreadHandle {
    explicit Thread(ThreadPoolExecutor* pool)
      : id(next_id++),
	handle(),
	idle(true),
	task_stats_subject(pool->task_stats_subject_) {}

    virtual ~Thread() {}

    static std::atomic<uint64_t> next_id;
    uint64_t id;
    std::thread handle;
    bool idle;
    Baton<> startup_baton;
    std::shared_ptr<Subject<TaskStats>> task_stats_subject;
  };

  typedef std::shared_ptr<Thread> ThreadPtr;

  struct Task {
    explicit Task(Func&& func,
                  std::chrono::milliseconds expiration,
		  Func&& expire_callback);

    Func func_;
    TaskStats stats_;
    std::chrono::steady_clock::time_point enqueue_time_;
    std::chrono::milliseconds expiration_;
    Func expire_callback_;
  };

  static void RunTask(const ThreadPtr& thread, Task&& task);
  virtual void ThreadRun(ThreadPtr thread) = 0;
  virtual void StopThreads(size_t n) = 0;
  virtual ThreadPtr MakeThread() {
    return std::make_shared<Thread>(this);
  }
  virtual uint64_t GetPendingTaskCount() = 0;

  class ThreadList {
   public:
    void Add(const ThreadPtr& state) {
      auto it = std::lower_bound(vec_.begin(), vec_.end(), state,
         [&](const ThreadPtr& t1, const ThreadPtr& t2) -> bool {
	   return Compare(t1, t2);
	 });
      vec_.insert(it, state);
    }

    void Remove(const ThreadPtr& state) {
      auto it_pair = std::equal_range(vec_.begin(),
		                      vec_.end(),
				      state,
          [&](const ThreadPtr& t1, const ThreadPtr& t2) -> bool {
	    return Compare(t1, t2);
	  });
      CHECK(it_pair.first != vec_.end());
      CHECK(std::next(it_pair.first) == it_pair.second);
      vec_.erase(it_pair.first);
    }

    const std::vector<ThreadPtr>& Get() const {
      return vec_;
    }

   private:
    static bool Compare(const ThreadPtr& t1, const ThreadPtr& t2) {
      return t1->id < t2->id;
    }
    std::vector<ThreadPtr> vec_;
  };

  class StoppedThreadQueue : public BlockingQueue<ThreadPtr> {
   public:
    void Add(ThreadPtr item) override;
    ThreadPtr Take() override;
    size_t Size() override;

   private:
    //TODO(wqx): LifoSem
    Semaphore sem_;
    std::mutex mutex_;
    std::queue<ThreadPtr> queue_;
  };

  std::shared_ptr<ThreadFactory> thread_factory_;
  ThreadList thread_list_;
  RWSpinLock thread_list_lock_;
  StoppedThreadQueue stopped_threads_;
  std::atomic<bool> is_join_;

  std::shared_ptr<Subject<TaskStats>> task_stats_subject_;
  std::vector<std::shared_ptr<Observer>> observers_;
};

} // namespace fb
