#ifndef BASE_WORKER_THREAD_H_
#define BASE_WORKER_THREAD_H_
#include "base/macros.h"
#include "base/thread.h"

#include <queue>

namespace base {

class TaskQueue;

class WorkerThread : public Thread {
 public:
  explicit WorkerThread(TaskQueue* queue);
  virtual ~WorkerThread();

  // Thread Implementation.
  void Run() override;


 private:
  //friend class QuitTask;
  
  TaskQueue* queue_;
  
  DISALLOW_COPY_AND_ASSIGN(WorkerThread);  
};

} // namespace base
#endif // BASE_WORKER_THREAD_H_
