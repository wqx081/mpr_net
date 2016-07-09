#ifndef WEBRTC_BASE_SIGNALTHREAD_H_
#define WEBRTC_BASE_SIGNALTHREAD_H_

#include <string>

#include "base/macros.h"
#include "net/sigslot.h"
#include "net/thread.h"


namespace net {

///////////////////////////////////////////////////////////////////////////////
// SignalThread - Base class for worker threads.  The main thread should call
//  Start() to begin work, and then follow one of these models:
//   Normal: Wait for SignalWorkDone, and then call Release to destroy.
//   Cancellation: Call Release(true), to abort the worker thread.
//   Fire-and-forget: Call Release(false), which allows the thread to run to
//    completion, and then self-destruct without further notification.
//   Periodic tasks: Wait for SignalWorkDone, then eventually call Start()
//    again to repeat the task. When the instance isn't needed anymore,
//    call Release. DoWork, OnWorkStart and OnWorkStop are called again,
//    on a new thread.
//  The subclass should override DoWork() to perform the background task.  By
//   periodically calling ContinueWork(), it can check for cancellation.
//   OnWorkStart and OnWorkDone can be overridden to do pre- or post-work
//   tasks in the context of the main thread.
///////////////////////////////////////////////////////////////////////////////

class SignalThread
    : public sigslot::has_slots<>,
      protected MessageHandler {
 public:
  SignalThread();

  // Context: Main Thread.  Call before Start to change the worker's name.
  bool SetName(const std::string& name, const void* obj);

  // Context: Main Thread.  Call to begin the worker thread.
  void Start();

  // Context: Main Thread.  If the worker thread is not running, deletes the
  // object immediately.  Otherwise, asks the worker thread to abort processing,
  // and schedules the object to be deleted once the worker exits.
  // SignalWorkDone will not be signalled.  If wait is true, does not return
  // until the thread is deleted.
  void Destroy(bool wait);

  // Context: Main Thread.  If the worker thread is complete, deletes the
  // object immediately.  Otherwise, schedules the object to be deleted once
  // the worker thread completes.  SignalWorkDone will be signalled.
  void Release();

  // Context: Main Thread.  Signalled when work is complete.
  sigslot::signal1<SignalThread *> SignalWorkDone;

  enum { ST_MSG_WORKER_DONE, ST_MSG_FIRST_AVAILABLE };

 protected:
  ~SignalThread() override;

  Thread* worker() { return &worker_; }

  // Context: Main Thread.  Subclass should override to do pre-work setup.
  virtual void OnWorkStart() { }

  // Context: Worker Thread.  Subclass should override to do work.
  virtual void DoWork() = 0;

  // Context: Worker Thread.  Subclass should call periodically to
  // dispatch messages and determine if the thread should terminate.
  bool ContinueWork();

  // Context: Worker Thread.  Subclass should override when extra work is
  // needed to abort the worker thread.
  virtual void OnWorkStop() { }

  // Context: Main Thread.  Subclass should override to do post-work cleanup.
  virtual void OnWorkDone() { }

  // Context: Any Thread.  If subclass overrides, be sure to call the base
  // implementation.  Do not use (message_id < ST_MSG_FIRST_AVAILABLE)
  void OnMessage(Message* msg) override;

 private:
  enum State {
    kInit,            // Initialized, but not started
    kRunning,         // Started and doing work
    kReleasing,       // Same as running, but to be deleted when work is done
    kComplete,        // Work is done
    kStopping,        // Work is being interrupted
  };

  class Worker : public Thread {
   public:
    explicit Worker(SignalThread* parent) : parent_(parent) {}
    ~Worker() override;
    void Run() override;

   private:
    SignalThread* parent_;

    Worker() = delete;
    DISALLOW_COPY_AND_ASSIGN(Worker);
  };

  class SCOPED_LOCKABLE EnterExit {
   public:
    explicit EnterExit(SignalThread* t) EXCLUSIVE_LOCK_FUNCTION(t->cs_)
        : t_(t) {
      t_->cs_.Enter();
      // If refcount_ is zero then the object has already been deleted and we
      // will be double-deleting it in ~EnterExit()! (shouldn't happen)
      assert(t_->refcount_ != 0);
      ++t_->refcount_;
    }
    ~EnterExit() UNLOCK_FUNCTION() {
      bool d = (0 == --t_->refcount_);
      t_->cs_.Leave();
      if (d)
        delete t_;
    }

   private:
    SignalThread* t_;

    EnterExit() = delete;
    DISALLOW_COPY_AND_ASSIGN(EnterExit);
  };

  void Run();
  void OnMainThreadDestroyed();

  Thread* main_;
  Worker worker_;
base::CriticalSection cs_;
  State state_;
  int refcount_;

  DISALLOW_COPY_AND_ASSIGN(SignalThread);
};

///////////////////////////////////////////////////////////////////////////////

}  // namespace rtc

#endif  // WEBRTC_BASE_SIGNALTHREAD_H_
