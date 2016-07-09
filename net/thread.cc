#include "net/thread.h"

#ifndef __has_feature
#define __has_feature(x) 0  // Compatibility with non-clang or LLVM compilers.
#endif  // __has_feature

#include <time.h>

#include "net/null_socket_server.h"
#include "net/platform_thread.h"
#include "net/thread.h"
#include "net/trace_event.h"
#include "base/timeutils.h"

#include <assert.h>

using namespace base;

namespace net {

ThreadManager* ThreadManager::Instance() {
  //RTC_DEFINE_STATIC_LOCAL(ThreadManager, thread_manager, ());
  static ThreadManager& thread_manager = *new ThreadManager();
  return &thread_manager;
}

// static
Thread* Thread::Current() {
  return ThreadManager::Instance()->CurrentThread();
}

ThreadManager::ThreadManager() {
  pthread_key_create(&key_, NULL);

#ifndef NO_MAIN_THREAD_WRAPPING
  WrapCurrentThread();
#endif
}

ThreadManager::~ThreadManager() {
  {
    UnwrapCurrentThread();
    pthread_key_delete(key_);
  }
}

Thread *ThreadManager::CurrentThread() {
  return static_cast<Thread *>(pthread_getspecific(key_));
}

void ThreadManager::SetCurrentThread(Thread *thread) {
  pthread_setspecific(key_, thread);
}

Thread *ThreadManager::WrapCurrentThread() {
  Thread* result = CurrentThread();
  if (NULL == result) {
    result = new Thread();
    result->WrapCurrentWithThreadManager(this, true);
  }
  return result;
}

void ThreadManager::UnwrapCurrentThread() {
  Thread* t = CurrentThread();
  if (t && !(t->IsOwned())) {
    t->UnwrapCurrent();
    delete t;
  }
}

struct ThreadInit {
  Thread* thread;
  Runnable* runnable;
};

Thread::ScopedDisallowBlockingCalls::ScopedDisallowBlockingCalls()
  : thread_(Thread::Current()),
    previous_state_(thread_->SetAllowBlockingCalls(false)) {
}

Thread::ScopedDisallowBlockingCalls::~ScopedDisallowBlockingCalls() {
  assert(thread_->IsCurrent());
  thread_->SetAllowBlockingCalls(previous_state_);
}

Thread::Thread() : Thread(SocketServer::CreateDefault()) {}

Thread::Thread(SocketServer* ss)
    : MessageQueue(ss, false),
      running_(true, false),
      owned_(true),
      blocking_calls_allowed_(true) {
  SetName("Thread", this);  // default name
  DoInit();
}

Thread::Thread(std::unique_ptr<SocketServer> ss)
    : MessageQueue(std::move(ss), false),
      running_(true, false),
      owned_(true),
      blocking_calls_allowed_(true) {
  SetName("Thread", this);  // default name
  DoInit();
}

Thread::~Thread() {
  Stop();
  DoDestroy();
}

std::unique_ptr<Thread> Thread::CreateWithSocketServer() {
  return std::unique_ptr<Thread>(new Thread(SocketServer::CreateDefault()));
}

std::unique_ptr<Thread> Thread::Create() {
  return std::unique_ptr<Thread>(
      new Thread(std::unique_ptr<SocketServer>(new NullSocketServer())));
}

bool Thread::SleepMs(int milliseconds) {
  AssertBlockingIsAllowedOnCurrentThread();

  // POSIX has both a usleep() and a nanosleep(), but the former is deprecated,
  // so we use nanosleep() even though it has greater precision than necessary.
  struct timespec ts;
  ts.tv_sec = milliseconds / 1000;
  ts.tv_nsec = (milliseconds % 1000) * 1000000;
  int ret = nanosleep(&ts, NULL);
  if (ret != 0) {
    LOG(WARNING) << "nanosleep() returning early";
    return false;
  }
  return true;
}

bool Thread::SetName(const std::string& name, const void* obj) {
  if (running()) return false;
  name_ = name;
  if (obj) {
    char buf[16];
    snprintf(buf, sizeof(buf), " 0x%p", obj);
    name_ += buf;
  }
  return true;
}

bool Thread::Start(Runnable* runnable) {
  assert(owned_);
  if (!owned_) return false;
  assert(!running());
  if (running()) return false;

  Restart();  // reset fStop_ if the thread is being restarted

  // Make sure that ThreadManager is created on the main thread before
  // we start a new thread.
  ThreadManager::Instance();

  ThreadInit* init = new ThreadInit;
  init->thread = this;
  init->runnable = runnable;
  pthread_attr_t attr;
  pthread_attr_init(&attr);

  int error_code = pthread_create(&thread_, &attr, PreRun, init);
  if (0 != error_code) {
    LOG(ERROR) << "Unable to create pthread, error " << error_code;
    return false;
  }
  running_.Set();
  return true;
}

bool Thread::WrapCurrent() {
  return WrapCurrentWithThreadManager(ThreadManager::Instance(), true);
}

void Thread::UnwrapCurrent() {
  // Clears the platform-specific thread-specific storage.
  ThreadManager::Instance()->SetCurrentThread(NULL);
  running_.Reset();
}

void Thread::SafeWrapCurrent() {
  WrapCurrentWithThreadManager(ThreadManager::Instance(), false);
}

void Thread::Join() {
  if (running()) {
    assert(!IsCurrent());
    if (Current() && !Current()->blocking_calls_allowed_) {
      LOG(WARNING) << "Waiting for the thread to join, "
                      << "but blocking calls have been disallowed";
    }

    void *pv;
    pthread_join(thread_, &pv);
    running_.Reset();
  }
}

bool Thread::SetAllowBlockingCalls(bool allow) {
  assert(IsCurrent());
  bool previous = blocking_calls_allowed_;
  blocking_calls_allowed_ = allow;
  return previous;
}

// static
void Thread::AssertBlockingIsAllowedOnCurrentThread() {
#if !defined(NDEBUG)
  Thread* current = Thread::Current();
  assert(!current || current->blocking_calls_allowed_);
#endif
}

void* Thread::PreRun(void* pv) {
  ThreadInit* init = static_cast<ThreadInit*>(pv);
  ThreadManager::Instance()->SetCurrentThread(init->thread);
  SetCurrentThreadName(init->thread->name_.c_str());
  {
    if (init->runnable) {
      init->runnable->Run(init->thread);
    } else {
      init->thread->Run();
    }
    delete init;
    return NULL;
  }
}

void Thread::Run() {
  ProcessMessages(kForever);
}

bool Thread::IsOwned() {
  return owned_;
}

void Thread::Stop() {
  MessageQueue::Quit();
  Join();
}

void Thread::Send(const base::Location& posted_from,
                  MessageHandler* phandler,
                  uint32_t id,
                  MessageData* pdata) {
  if (fStop_)
    return;

  // Sent messages are sent to the MessageHandler directly, in the context
  // of "thread", like Win32 SendMessage. If in the right context,
  // call the handler directly.
  Message msg;
  msg.posted_from = posted_from;
  msg.phandler = phandler;
  msg.message_id = id;
  msg.pdata = pdata;
  if (IsCurrent()) {
    phandler->OnMessage(&msg);
    return;
  }

  AssertBlockingIsAllowedOnCurrentThread();

  AutoThread thread;
  Thread *current_thread = Thread::Current();
  assert(current_thread != NULL);  // AutoThread ensures this

  bool ready = false;
  {
base::CritScope cs(&crit_);
    _SendMessage smsg;
    smsg.thread = current_thread;
    smsg.msg = msg;
    smsg.ready = &ready;
    sendlist_.push_back(smsg);
  }

  // Wait for a reply
  WakeUpSocketServer();

  bool waited = false;
  crit_.Enter();
  while (!ready) {
    crit_.Leave();
    // We need to limit "ReceiveSends" to |this| thread to avoid an arbitrary
    // thread invoking calls on the current thread.
    current_thread->ReceiveSendsFromThread(this);
    current_thread->socketserver()->Wait(kForever, false);
    waited = true;
    crit_.Enter();
  }
  crit_.Leave();

  // Our Wait loop above may have consumed some WakeUp events for this
  // MessageQueue, that weren't relevant to this Send.  Losing these WakeUps can
  // cause problems for some SocketServers.
  //
  // Concrete example:
  // Win32SocketServer on thread A calls Send on thread B.  While processing the
  // message, thread B Posts a message to A.  We consume the wakeup for that
  // Post while waiting for the Send to complete, which means that when we exit
  // this loop, we need to issue another WakeUp, or else the Posted message
  // won't be processed in a timely manner.

  if (waited) {
    current_thread->socketserver()->WakeUp();
  }
}

void Thread::ReceiveSends() {
  ReceiveSendsFromThread(NULL);
}

void Thread::ReceiveSendsFromThread(const Thread* source) {
  // Receive a sent message. Cleanup scenarios:
  // - thread sending exits: We don't allow this, since thread can exit
  //   only via Join, so Send must complete.
  // - thread receiving exits: Wakeup/set ready in Thread::Clear()
  // - object target cleared: Wakeup/set ready in Thread::Clear()
  _SendMessage smsg;

  crit_.Enter();
  while (PopSendMessageFromThread(source, &smsg)) {
    crit_.Leave();

    smsg.msg.phandler->OnMessage(&smsg.msg);

    crit_.Enter();
    *smsg.ready = true;
    smsg.thread->socketserver()->WakeUp();
  }
  crit_.Leave();
}

bool Thread::PopSendMessageFromThread(const Thread* source, _SendMessage* msg) {
  for (std::list<_SendMessage>::iterator it = sendlist_.begin();
       it != sendlist_.end(); ++it) {
    if (it->thread == source || source == NULL) {
      *msg = *it;
      sendlist_.erase(it);
      return true;
    }
  }
  return false;
}

void Thread::InvokeInternal(const base::Location& posted_from,
                            MessageHandler* handler) {
  TRACE_EVENT2("webrtc", "Thread::Invoke", "src_file_and_line",
               posted_from.file_and_line(), "src_func",
               posted_from.function_name());
  Send(posted_from, handler);
}

void Thread::Clear(MessageHandler* phandler,
                   uint32_t id,
                   MessageList* removed) {
  CritScope cs(&crit_);

  // Remove messages on sendlist_ with phandler
  // Object target cleared: remove from send list, wakeup/set ready
  // if sender not NULL.

  std::list<_SendMessage>::iterator iter = sendlist_.begin();
  while (iter != sendlist_.end()) {
    _SendMessage smsg = *iter;
    if (smsg.msg.Match(phandler, id)) {
      if (removed) {
        removed->push_back(smsg.msg);
      } else {
        delete smsg.msg.pdata;
      }
      iter = sendlist_.erase(iter);
      *smsg.ready = true;
      smsg.thread->socketserver()->WakeUp();
      continue;
    }
    ++iter;
  }

  MessageQueue::Clear(phandler, id, removed);
}

bool Thread::ProcessMessages(int cmsLoop) {
  int64_t msEnd = (kForever == cmsLoop) ? 0 : TimeAfter(cmsLoop);
  int cmsNext = cmsLoop;

  while (true) {
    {
      Message msg;
      if (!Get(&msg, cmsNext))
        return !IsQuitting();
      Dispatch(&msg);

      if (cmsLoop != kForever) {
        cmsNext = static_cast<int>(TimeUntil(msEnd));
        if (cmsNext < 0)
          return true;
      }
    }
  }
}

bool Thread::WrapCurrentWithThreadManager(ThreadManager* thread_manager,
                                          bool need_synchronize_access) {
  (void) need_synchronize_access;
  if (running())
    return false;

  thread_ = pthread_self();

  owned_ = false;
  running_.Set();
  thread_manager->SetCurrentThread(this);
  return true;
}

AutoThread::AutoThread() {
  if (!ThreadManager::Instance()->CurrentThread()) {
    ThreadManager::Instance()->SetCurrentThread(this);
  }
}

AutoThread::~AutoThread() {
  Stop();
  if (ThreadManager::Instance()->CurrentThread() == this) {
    ThreadManager::Instance()->SetCurrentThread(NULL);
  }
}

}  // namespace rtc
