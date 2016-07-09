#include "net/platform_thread.h"


#include <sys/prctl.h>
#include <sys/syscall.h>

namespace net {

PlatformThreadId CurrentThreadId() {
  PlatformThreadId ret;
  ret =  syscall(__NR_gettid);
  DCHECK(ret);
  return ret;
}

PlatformThreadRef CurrentThreadRef() {
  return pthread_self();
}

bool IsThreadRefEqual(const PlatformThreadRef& a, const PlatformThreadRef& b) {
  return pthread_equal(a, b);
}

void SetCurrentThreadName(const char* name) {
  prctl(PR_SET_NAME, reinterpret_cast<unsigned long>(name));
}

namespace {
struct ThreadAttributes {
  ThreadAttributes() { pthread_attr_init(&attr); }
  ~ThreadAttributes() { pthread_attr_destroy(&attr); }
  pthread_attr_t* operator&() { return &attr; }
  pthread_attr_t attr;
};
}

PlatformThread::PlatformThread(ThreadRunFunction func,
                               void* obj,
                               const char* thread_name)
    : run_function_(func),
      obj_(obj),
      name_(thread_name ? thread_name : "webrtc"),
      stop_event_(false, false),
      thread_(0) {
  DCHECK(func);
  DCHECK(name_.length() < 64);
}

PlatformThread::~PlatformThread() {
  DCHECK(thread_checker_.CalledOnValidThread());
}

void* PlatformThread::StartThread(void* param) {
  static_cast<PlatformThread*>(param)->Run();
  return 0;
}

void PlatformThread::Start() {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(!thread_) << "Thread already started?";
  ThreadAttributes attr;
  // Set the stack stack size to 1M.
  pthread_attr_setstacksize(&attr, 1024 * 1024);
  CHECK_EQ(0, pthread_create(&thread_, &attr, &StartThread, this));
}

bool PlatformThread::IsRunning() const {
  DCHECK(thread_checker_.CalledOnValidThread());
  return thread_ != 0;
}

PlatformThreadRef PlatformThread::GetThreadRef() const {
  return thread_;
}

void PlatformThread::Stop() {
  DCHECK(thread_checker_.CalledOnValidThread());
  if (!IsRunning())
    return;

  stop_event_.Set();
  CHECK_EQ(0, pthread_join(thread_, nullptr));
  thread_ = 0;
}

void PlatformThread::Run() {
  if (!name_.empty())
    net::SetCurrentThreadName(name_.c_str());
  do {
    // The interface contract of Start/Stop is that for a successful call to
    // Start, there should be at least one call to the run function.  So we
    // call the function before checking |stop_|.
    if (!run_function_(obj_))
      break;
  } while (!stop_event_.Wait(0));
}

bool PlatformThread::SetPriority(ThreadPriority priority) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(IsRunning());
#ifdef WEBRTC_THREAD_RR
  const int policy = SCHED_RR;
#else
  const int policy = SCHED_FIFO;
#endif
  const int min_prio = sched_get_priority_min(policy);
  const int max_prio = sched_get_priority_max(policy);
  if (min_prio == -1 || max_prio == -1) {
    return false;
  }

  if (max_prio - min_prio <= 2)
    return false;

  // Convert webrtc priority to system priorities:
  sched_param param;
  const int top_prio = max_prio - 1;
  const int low_prio = min_prio + 1;
  switch (priority) {
    case kLowPriority:
      param.sched_priority = low_prio;
      break;
    case kNormalPriority:
      // The -1 ensures that the kHighPriority is always greater or equal to
      // kNormalPriority.
      param.sched_priority = (low_prio + top_prio - 1) / 2;
      break;
    case kHighPriority:
      param.sched_priority = std::max(top_prio - 2, low_prio);
      break;
    case kHighestPriority:
      param.sched_priority = std::max(top_prio - 1, low_prio);
      break;
    case kRealtimePriority:
      param.sched_priority = top_prio;
      break;
  }
  return pthread_setschedparam(thread_, policy, &param) == 0;
}

}  // namespace rtc
