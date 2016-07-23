#include "common/thread_pool_executor.h"

namespace fb {

ThreadPoolExecutor::ThreadPoolExecutor(size_t num_threads,
    std::shared_ptr<ThreadFactory> thread_factory)
    : thread_factory_(std::move(thread_factory)),
      task_stats_subject_(std::make_shared<Subject<TaskStats>>()) {
  (void)num_threads;
}

ThreadPoolExecutor::~ThreadPoolExecutor() {
  CHECK(thread_list_.Get().size() == 0);
}

ThreadPoolExecutor::Task::Task(Func&& func,
    std::chrono::milliseconds expiration,
    Func&& expire_callback)
    : func_(std::move(func)),
      expiration_(expiration),
      expire_callback_(std::move(expire_callback)) {
  enqueue_time_ = std::chrono::steady_clock::now();
}	

void ThreadPoolExecutor::RunTask(const ThreadPtr& thread,
    Task&& task) {
  thread->idle = false;
  auto start_time = std::chrono::steady_clock::now();
  task.stats_.wait_time = start_time - task.enqueue_time_;
  if (task.expiration_ > std::chrono::milliseconds(0) &&
      task.stats_.wait_time >= task.expiration_) {
    task.stats_.expired = true;
    if (task.expire_callback_ != nullptr) {
      task.expire_callback_();
    }
  } else {
    try {
      task.func_();
    } catch (const std::exception& e) {
      LOG(ERROR) << "ThreadPoolExecutor: func threw unhandled " <<
	      typeid(e).name() << " exception: " << e.what();
    } catch (...) {
      LOG(ERROR) << "ThreadPoolExecutor: func threw unhandled non-exception "
	      "object";
    }
    task.stats_.run_time = std::chrono::steady_clock::now() - start_time;
  }
  thread->idle = true;
  thread->task_stats_subject->OnNext(std::move(task.stats_));
}		 

size_t ThreadPoolExecutor::num_threads() {
  RWSpinLock::ReadHolder{&thread_list_lock_};
  return thread_list_.Get().size();
}

void ThreadPoolExecutor::SetNumThreads(size_t n) {
  RWSpinLock::WriteHolder{&thread_list_lock_};
  const auto current = thread_list_.Get().size();
  if (n > current) {
    AddThreads(n - current);
  } else if (n < current) {
    RemoveThreads(current - n, true);
  }
  CHECK(thread_list_.Get().size() == n);
}

void ThreadPoolExecutor::AddThreads(size_t n) {
  std::vector<ThreadPtr> new_threads;
  for (size_t i = 0; i < n; ++i) {
    new_threads.push_back(MakeThread());
  }
  for (auto& thread : new_threads) {
    thread->handle = thread_factory_->NewThread(
      std::bind(&ThreadPoolExecutor::ThreadRun, this, thread));
    thread_list_.Add(thread);
  }
  for (auto& thread : new_threads) {
    thread->startup_baton.wait();
  }
  for (auto& o : observers_) {
    for (auto& thread : new_threads) {
      o->ThreadStarted(thread.get());
    }
  }
}

void ThreadPoolExecutor::RemoveThreads(size_t n, bool is_join) {
  CHECK(n <= thread_list_.Get().size());
  CHECK(stopped_threads_.Size() == 0);
  is_join_ = is_join;
  StopThreads(n);
  for (size_t i = 0; i < n; ++i) {
    auto thread = stopped_threads_.Take();
    thread->handle.join();
    thread_list_.Remove(thread);
  }
  CHECK(stopped_threads_.Size() == 0);
}


void ThreadPoolExecutor::Stop() {
  RWSpinLock::WriteHolder{&thread_list_lock_};
  RemoveThreads(thread_list_.Get().size(), false);
  CHECK(thread_list_.Get().size() == 0);
}

void ThreadPoolExecutor::Join() {
  RWSpinLock::WriteHolder{&thread_list_lock_};
  RemoveThreads(thread_list_.Get().size(), true);
  CHECK(thread_list_.Get().size() == 0);
}

ThreadPoolExecutor::PoolStats ThreadPoolExecutor::GetPoolStats() {
  RWSpinLock::ReadHolder{&thread_list_lock_};
  ThreadPoolExecutor::PoolStats stats;
  stats.thread_count = thread_list_.Get().size();
  for (auto thread : thread_list_.Get()) {
    if (thread->idle) { 
      stats.idle_thread_count++;
    } else {
      stats.active_thread_count++;
    }
  }
  stats.pending_task_count = GetPendingTaskCount();
  stats.total_task_count = stats.pending_task_count +
	                   stats.active_thread_count;
  return stats;
}

std::atomic<uint64_t> ThreadPoolExecutor::Thread::next_id(0);

void ThreadPoolExecutor::StoppedThreadQueue::Add(ThreadPtr item) {
  std::lock_guard<std::mutex> guard(mutex_);
  queue_.push(std::move(item));
  sem_.Post();
}

ThreadPoolExecutor::ThreadPtr ThreadPoolExecutor::StoppedThreadQueue::Take() {
  while (1) {
    {
      std::lock_guard<std::mutex> g(mutex_);
      if (queue_.size() > 0) {
        auto item = std::move(queue_.front());
        queue_.pop();
        return item;
      }
    }
    sem_.Wait();
  }
}

size_t ThreadPoolExecutor::StoppedThreadQueue::Size() {
  std::lock_guard<std::mutex> g(mutex_);
  return queue_.size();
}

void ThreadPoolExecutor::AddObserver(std::shared_ptr<Observer> o) {
  RWSpinLock::ReadHolder{&thread_list_lock_};
  observers_.push_back(o);
  for (auto& thread : thread_list_.Get()) {
    o->ThreadPreviouslyStarted(thread.get());
  }
}

void ThreadPoolExecutor::RemoveObserver(std::shared_ptr<Observer> o) {
  RWSpinLock::ReadHolder{&thread_list_lock_};
  for (auto& thread : thread_list_.Get()) {
    o->ThreadNotYetStopped(thread.get());
  }

  for (auto it = observers_.begin(); it != observers_.end(); ++it) {
    if (*it == o) {
      observers_.erase(it);
      return;
    }
  }
  DCHECK(false);
}

} // namespace fb
