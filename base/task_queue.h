#ifndef BASE_TASK_QUEUE_H_
#define BASE_TASK_QUEUE_H_

#include "base/macros.h"
#include "base/mutex.h"
#include "base/semaphore.h"

#include <queue>

namespace base {

class Task;

class TaskQueue {
 public:
  TaskQueue();
  ~TaskQueue();

  void Append(Task* task);
  Task* GetNext();
  void Terminate();

 private:
  Semaphore process_queue_semaphore_;
  Mutex lock_;
  std::queue<Task*> task_queue_;
  bool terminated_;

  DISALLOW_COPY_AND_ASSIGN(TaskQueue);
};

} // namespace base
#endif // BASE_TASK_QUEUE_H_
