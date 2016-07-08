#include "base/worker_thread.h"
#include "base/task.h"
#include "base/task_queue.h"

namespace base {

WorkerThread::WorkerThread(TaskQueue* queue)
    : Thread(Options("Worker-thread")),
      queue_(queue) {
  Start();
}

WorkerThread::~WorkerThread() {
  Join();
}

void WorkerThread::Run() {
  while (Task* task = queue_->GetNext()) {
    task->Run();
    delete task;
  }
}

} // namespace
