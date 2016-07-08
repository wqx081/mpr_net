#include "base/thread.h"
#include "base/mutex.h"
#include "base/task_queue.h"

#include <gtest/gtest.h>

namespace base {

class SimpleThread : public Thread {
 public:
  explicit SimpleThread(const Thread::Options& options)
      : Thread(options) {
  }

  virtual void Run() override {
    LOG(INFO) << "\t\t\tHello, World";
    sleep(1000);
  }
};

bool SimpleThreadTest() {
  Thread::Options thread_options("SimpleThread"); // Check this use "ps -e -T | grep SimpleThread"
  SimpleThread thread(thread_options);
  thread.Start();
  thread.Join(); // Wait the thread done work!
  LOG(INFO) << "\t\t\tSuccess!";
  return true;
}

TEST(Thread, Create) {
  EXPECT_TRUE(SimpleThreadTest());
}

class RecordIdThread : public Thread {
 public:
  explicit RecordIdThread(const Thread::Options& options)
      : Thread(options) {
  }
  void Run() override {
    id_ = Thread::GetCurrentThreadId();
  }

  Thread::ThreadId id_;
};

//////////////////////////////////////////////////////////////


} // namespace base
