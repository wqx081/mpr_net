#include "net/signal_thread.h"


namespace net {

///////////////////////////////////////////////////////////////////////////////
// SignalThread
///////////////////////////////////////////////////////////////////////////////

SignalThread::SignalThread()
    : main_(Thread::Current()),
      worker_(this),
      state_(kInit),
      refcount_(1) {
  main_->SignalQueueDestroyed.connect(this,
                                      &SignalThread::OnMainThreadDestroyed);
  worker_.SetName("SignalThread", this);
}

SignalThread::~SignalThread() {
  assert(refcount_ == 0);
}

bool SignalThread::SetName(const std::string& name, const void* obj) {
  EnterExit ee(this);
  assert(main_->IsCurrent());
  assert(kInit == state_);
  return worker_.SetName(name, obj);
}

void SignalThread::Start() {
  EnterExit ee(this);
  assert(main_->IsCurrent());
  if (kInit == state_ || kComplete == state_) {
    state_ = kRunning;
    OnWorkStart();
    worker_.Start();
  } else {
    assert(false);
  }
}

void SignalThread::Destroy(bool wait) {
  EnterExit ee(this);
  assert(main_->IsCurrent());
  if ((kInit == state_) || (kComplete == state_)) {
    refcount_--;
  } else if (kRunning == state_ || kReleasing == state_) {
    state_ = kStopping;
    // OnWorkStop() must follow Quit(), so that when the thread wakes up due to
    // OWS(), ContinueWork() will return false.
    worker_.Quit();
    OnWorkStop();
    if (wait) {
      // Release the thread's lock so that it can return from ::Run.
      cs_.Leave();
      worker_.Stop();
      cs_.Enter();
      refcount_--;
    }
  } else {
    assert(false);
  }
}

void SignalThread::Release() {
  EnterExit ee(this);
  assert(main_->IsCurrent());
  if (kComplete == state_) {
    refcount_--;
  } else if (kRunning == state_) {
    state_ = kReleasing;
  } else {
    // if (kInit == state_) use Destroy()
    assert(false);
  }
}

bool SignalThread::ContinueWork() {
  EnterExit ee(this);
  assert(worker_.IsCurrent());
  return worker_.ProcessMessages(0);
}

void SignalThread::OnMessage(Message *msg) {
  EnterExit ee(this);
  if (ST_MSG_WORKER_DONE == msg->message_id) {
    assert(main_->IsCurrent());
    OnWorkDone();
    bool do_delete = false;
    if (kRunning == state_) {
      state_ = kComplete;
    } else {
      do_delete = true;
    }
    if (kStopping != state_) {
      // Before signaling that the work is done, make sure that the worker
      // thread actually is done. We got here because DoWork() finished and
      // Run() posted the ST_MSG_WORKER_DONE message. This means the worker
      // thread is about to go away anyway, but sometimes it doesn't actually
      // finish before SignalWorkDone is processed, and for a reusable
      // SignalThread this makes an assert in thread.cc fire.
      //
      // Calling Stop() on the worker ensures that the OS thread that underlies
      // the worker will finish, and will be set to NULL, enabling us to call
      // Start() again.
      worker_.Stop();
      SignalWorkDone(this);
    }
    if (do_delete) {
      refcount_--;
    }
  }
}

SignalThread::Worker::~Worker() {
  Stop();
}

void SignalThread::Worker::Run() {
  parent_->Run();
}

void SignalThread::Run() {
  DoWork();
  {
    EnterExit ee(this);
    if (main_) {
      main_->Post(FROM_HERE, this, ST_MSG_WORKER_DONE);
    }
  }
}

void SignalThread::OnMainThreadDestroyed() {
  EnterExit ee(this);
  main_ = NULL;
}

}  // namespace rtc
