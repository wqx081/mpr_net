#include "base/task_queue.h"

namespace base {

TaskQueue::TaskQueue() : 
    process_queue_semaphore_(0),
    terminated_(false) {
}

TaskQueue::~TaskQueue() {
  LockGuard<Mutex> guard(&lock_);
  DCHECK(terminated_);
  DCHECK(task_queue_.empty());
}

void TaskQueue::Append(Task* task) {
  LockGuard<Mutex> g(&lock_);
  DCHECK(!terminated_);
  task_queue_.push(task);
  process_queue_semaphore_.Signal();
}

Task* TaskQueue::GetNext() {
  for (;;) {
    {
      LockGuard<Mutex> g(&lock_);
      if (!task_queue_.empty()) {
        Task* result = task_queue_.front();
	task_queue_.pop();
	return result;
      }   
      if (terminated_) {
        process_queue_semaphore_.Signal();
	return nullptr;
      }
    }
    process_queue_semaphore_.Wait();
  }
}

void TaskQueue::Terminate() {
  LockGuard<Mutex> g(&lock_);
  DCHECK(!terminated_);
  terminated_ = true;
  process_queue_semaphore_.Signal();
}

} // namespace base
