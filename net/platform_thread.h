#ifndef WEBRTC_BASE_PLATFORM_THREAD_H_
#define WEBRTC_BASE_PLATFORM_THREAD_H_

#include <string>


#include "base/macros.h"
#include "net/event.h"
#include "net/platform_thread_types.h"
#include "net/thread_checker.h"

namespace net {

PlatformThreadId CurrentThreadId();
PlatformThreadRef CurrentThreadRef();

// Compares two thread identifiers for equality.
bool IsThreadRefEqual(const PlatformThreadRef& a, const PlatformThreadRef& b);

// Sets the current thread name.
void SetCurrentThreadName(const char* name);

// Callback function that the spawned thread will enter once spawned.
// A return value of false is interpreted as that the function has no
// more work to do and that the thread can be released.
typedef bool (*ThreadRunFunction)(void*);

enum ThreadPriority {
  kLowPriority = 1,
  kNormalPriority = 2,
  kHighPriority = 3,
  kHighestPriority = 4,
  kRealtimePriority = 5
};

// Represents a simple worker thread.  The implementation must be assumed
// to be single threaded, meaning that all methods of the class, must be
// called from the same thread, including instantiation.
class PlatformThread {
 public:
  PlatformThread(ThreadRunFunction func, void* obj, const char* thread_name);
  virtual ~PlatformThread();

  const std::string& name() const { return name_; }

  // Spawns a thread and tries to set thread priority according to the priority
  // from when CreateThread was called.
  void Start();

  bool IsRunning() const;

  // Returns an identifier for the worker thread that can be used to do
  // thread checks.
  PlatformThreadRef GetThreadRef() const;

  // Stops (joins) the spawned thread.
  void Stop();

  // Set the priority of the thread. Must be called when thread is running.
  bool SetPriority(ThreadPriority priority);

 protected:

 private:
  void Run();

  ThreadRunFunction const run_function_;
  void* const obj_;
  // TODO(pbos): Make sure call sites use string literals and update to a const
  // char* instead of a std::string.
  const std::string name_;
  net::ThreadChecker thread_checker_;
  static void* StartThread(void* param);

  net::Event stop_event_;

  pthread_t thread_;
  DISALLOW_COPY_AND_ASSIGN(PlatformThread);
};

}  // namespace rtc

#endif  // WEBRTC_BASE_PLATFORM_THREAD_H_
