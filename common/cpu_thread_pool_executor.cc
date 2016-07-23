#include "common/cpu_thread_pool_executor.h"
#include "common/priority_mpmc_queue.h"
#include "common/lifo_sem_mpmc_queue.h"

namespace fb {

const size_t CPUThreadPoolExecutor::kDefaultMaxQueueSize = 1 << 14;
const size_t CPUThreadPoolExecutor::kDefaultNumPriorities = 2;


CPUThreadPoolExecutor::CPUThreadPoolExecutor(size_t num_threads,
    std::unique_ptr<BlockingQueue<CPUTask>> task_queue,
    std::shared_ptr<ThreadFactory> thread_factory)
    : ThreadPoolExecutor(num_threads, std::move(thread_factory)),
      task_queue_(std::move(task_queue)) {
  AddThreads(num_threads);
  CHECK(thread_list_.Get().size() == num_threads);
}

CPUThreadPoolExecutor::CPUThreadPoolExecutor(
    size_t num_threads,
    std::shared_ptr<ThreadFactory> thread_factory)
    : CPUThreadPoolExecutor(num_threads,
		make_unique<LifoSemMPMCQueue<CPUTask>>(
	CPUThreadPoolExecutor::kDefaultMaxQueueSize),
	std::move(thread_factory)) {}
	        
CPUThreadPoolExecutor::CPUThreadPoolExecutor(size_t numThreads)
	      : CPUThreadPoolExecutor(numThreads,
                std::make_shared<NamedThreadFactory>("CPUThreadPool")) {}
	        
CPUThreadPoolExecutor::CPUThreadPoolExecutor(
size_t numThreads,
uint32_t numPriorities,
std::shared_ptr<ThreadFactory> threadFactory)
	      : CPUThreadPoolExecutor(
numThreads,
make_unique<PriorityLifoSemMPMCQueue<CPUTask>>(
numPriorities,
CPUThreadPoolExecutor::kDefaultMaxQueueSize),
std::move(threadFactory)) {}
	        
CPUThreadPoolExecutor::CPUThreadPoolExecutor(
size_t numThreads,
uint32_t numPriorities,
size_t maxQueueSize,
std::shared_ptr<ThreadFactory> threadFactory)
: CPUThreadPoolExecutor(
numThreads,
make_unique<PriorityLifoSemMPMCQueue<CPUTask>>(
numPriorities,
maxQueueSize),
std::move(threadFactory)) {}


CPUThreadPoolExecutor::~CPUThreadPoolExecutor() {
  Stop();
  CHECK(threads_to_stop_ == 0);
}

void CPUThreadPoolExecutor::Add(Func func) {
  Add(std::move(func), std::chrono::milliseconds(0));
}

void CPUThreadPoolExecutor::Add(Func func,
		                std::chrono::milliseconds expiration,
				Func expire_callback) {
  task_queue_->Add(CPUTask(std::move(func), expiration,
                   std::move(expire_callback)));
}

void CPUThreadPoolExecutor::Add(Func func, uint32_t priority) {
  Add(std::move(func), priority, std::chrono::milliseconds(0));
}

void CPUThreadPoolExecutor::Add(Func func,
		                uint32_t priority,
				std::chrono::milliseconds expiration,
				Func expire_callback) {
  CHECK(priority < GetNumPriorities());
  task_queue_->AddWithPriority(
    CPUTask(std::move(func), expiration, std::move(expire_callback)),
    priority);
}

uint32_t CPUThreadPoolExecutor::GetNumPriorities() const {
  return task_queue_->GetNumPriorities();
}

BlockingQueue<CPUThreadPoolExecutor::CPUTask>*
CPUThreadPoolExecutor::GetTaskQueue() {
  return task_queue_.get();
}

void CPUThreadPoolExecutor::ThreadRun(std::shared_ptr<Thread> thread) {
  thread->startup_baton.post();
  while (true) {
    auto task = task_queue_->Take();
    if (MPR_UNLIKELY(task.poison)) {
      CHECK(threads_to_stop_-- > 0);
      for (auto& o : observers_) {
        o->ThreadStopped(thread.get());
      }

      stopped_threads_.Add(thread);
      return;
    } else {
      RunTask(thread, std::move(task));
    }

    if (MPR_UNLIKELY(threads_to_stop_ > 0 && !is_join_)) {
      if (--threads_to_stop_ >= 0) {
        stopped_threads_.Add(thread);
	return;
      } else {
        threads_to_stop_++;
      }
    }
  }
}

void CPUThreadPoolExecutor::StopThreads(size_t n) {
  CHECK(stopped_threads_.Size() == 0);
  threads_to_stop_ = n;
  for (size_t i = 0; i < n; ++i) {
    task_queue_->Add(CPUTask());
  }
}

uint64_t CPUThreadPoolExecutor::GetPendingTaskCount() {
  return task_queue_->Size();
}

} // namespace fb
