#include "base/thread.h"
#include "base/time.h"
#include "base/lazy_instance.h"


#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <sys/prctl.h>
#include <sys/syscall.h>

namespace base {

namespace {

const pthread_t kNoThread = (pthread_t) 0;

} // namespace

// static
int Thread::GetCurrentProcessId() {
  return static_cast<int>(getpid());
}
Thread::ThreadId Thread::GetCurrentThreadId() {
  return static_cast<Thread::ThreadId>(syscall(__NR_gettid));
}

class Thread::PlatformData {
 public:
  PlatformData() : thread_(kNoThread) {}

  pthread_t thread_;
  Mutex thread_creation_mutex_;
};

Thread::Thread(const Options& options) 
    : data_(new PlatformData),
      stack_size_(options.stack_size()),
      start_semaphore_(nullptr) {
  if (stack_size_ > 0 && 
      static_cast<size_t>(stack_size_) < PTHREAD_STACK_MIN) {
    stack_size_ = PTHREAD_STACK_MIN;
  }
  set_name(options.name());
}

Thread::~Thread() {
  delete data_;
}

static void SetThreadName(const char* name) {
  prctl(PR_SET_NAME,
        reinterpret_cast<unsigned long>(name),
	0, 0, 0);
}

static void* ThreadEntry(void* arg) {
  Thread* thread = reinterpret_cast<Thread*>(arg);

  { 
    LockGuard<Mutex> lock_guard(&thread->data()->thread_creation_mutex_);
  }
  SetThreadName(thread->name());
  DCHECK(thread->data()->thread_ != kNoThread);
  thread->NotifyStartedAndRun();
  return nullptr;
}

void Thread::set_name(const char* name) {
  strncpy(name_, name, sizeof(name_));
  name_[sizeof(name_) - 1] = '\0';
}

void Thread::Start() {
  int result;
  pthread_attr_t attr;
  memset(&attr, 0, sizeof(attr));
  result = pthread_attr_init(&attr);
  DCHECK_EQ(0, result);
  size_t stack_size = stack_size_;
  if (stack_size > 0) {
    result = pthread_attr_setstacksize(&attr, stack_size);
    DCHECK_EQ(0, result);
  }
  {
    LockGuard<Mutex> lock_guard(&data_->thread_creation_mutex_);
    result = pthread_create(&data_->thread_,
		            &attr,
			    ThreadEntry,
			    this);
  }
  DCHECK_EQ(0, result);
  result = pthread_attr_destroy(&attr);
  DCHECK_EQ(0, result);
  DCHECK(data_->thread_ != kNoThread);
}

void Thread::Join() {
  pthread_join(data_->thread_, nullptr);
}

static Thread::LocalStorageKey PthreadKeyToLocalKey(pthread_key_t pthread_key) {
  return static_cast<Thread::LocalStorageKey>(pthread_key);
}
static pthread_key_t LocalKeyToPthreadKey(Thread::LocalStorageKey local_key) {
  return static_cast<pthread_key_t>(local_key);
}

// static 
Thread::LocalStorageKey Thread::CreateThreadLocalKey() {
  pthread_key_t key;
  int result = pthread_key_create(&key, nullptr);
  DCHECK_EQ(0, result);
  LocalStorageKey local_key = PthreadKeyToLocalKey(key);
  return local_key;
}

void Thread::DeleteThreadLocalkey(LocalStorageKey key) {
  pthread_key_t pthread_key = LocalKeyToPthreadKey(key);
  int result = pthread_key_delete(pthread_key);
  DCHECK_EQ(0, result);
}

void* Thread::GetThreadLocal(LocalStorageKey key) {
  pthread_key_t pthread_key = LocalKeyToPthreadKey(key);
  return pthread_getspecific(pthread_key);
}


void Thread::SetThreadLocal(LocalStorageKey key, void* value) {
  pthread_key_t pthread_key = LocalKeyToPthreadKey(key);
  int result = pthread_setspecific(pthread_key, value);
  DCHECK_EQ(0, result);
  USE(result);
}


} // namespace base
