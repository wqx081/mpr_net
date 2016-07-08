#ifndef BASE_THREAD_H_
#define BASE_THREAD_H_

#include "base/macros.h"
#include "base/mutex.h"
#include "base/semaphore.h"


namespace base {

class Thread {
 public:
  typedef int32_t LocalStorageKey;
  typedef uint64_t ThreadId;

  class Options {
   public:
    Options() : name_("thread:<>"), stack_size_(0) {}
    explicit Options(const char* name, int stack_size = 0)
        : name_(name), stack_size_(stack_size) {}

    const char* name() const { return name_; }
    int stack_size() const { return stack_size_; }

   private:
    const char* name_;
    int stack_size_;
  };

  // Create new thread.
  explicit Thread(const Options& options);
  virtual ~Thread();

  void Start();
  void StartSynchronously() {
    start_semaphore_ = new Semaphore(0);
    Start();
    start_semaphore_->Wait();
    delete start_semaphore_;
    start_semaphore_ = nullptr;
  }

  // Wait until thread terminates.
  void Join();
  inline const char* name() const {
    return name_;
  }

  virtual void Run() = 0;

  // Misc
  static int GetCurrentProcessId();
  static ThreadId GetCurrentThreadId();

  // Thread-Local storage.
  static LocalStorageKey CreateThreadLocalKey();
  static void DeleteThreadLocalkey(LocalStorageKey key);
  static void* GetThreadLocal(LocalStorageKey key);
  static int GetThreadLocalInt(LocalStorageKey key) {
    return static_cast<int>(reinterpret_cast<intptr_t>(GetThreadLocal(key)));
  }
  static void SetThreadLocal(LocalStorageKey key, void* value);
  static void SetThreadLocalInt(LocalStorageKey key, int value) {
    SetThreadLocal(key,
                   reinterpret_cast<void*>(static_cast<intptr_t>(value)));
  }
  static bool HasThreadLocal(LocalStorageKey key) {
    return GetThreadLocal(key) != nullptr;
  }
  static inline void* GetExistingThreadLocal(LocalStorageKey key) {
    return GetThreadLocal(key);
  }


  static const int kMaxThreadNameLength = 16;

  class PlatformData;
  PlatformData* data() { return data_; }

  void NotifyStartedAndRun() {
    if (start_semaphore_) {
      start_semaphore_->Signal();
    }
    Run();
  }

 private:
  void set_name(const char* name);

  PlatformData* data_;

  char name_[kMaxThreadNameLength];
  int stack_size_;
  Semaphore* start_semaphore_;

  DISALLOW_COPY_AND_ASSIGN(Thread);
};

} // namespace base
#endif // BASE_THREAD_H_
