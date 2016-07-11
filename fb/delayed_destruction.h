#ifndef FB_DELAYED_DESTRUCTION_H_
#define FB_DELAYED_DESTRUCTION_H_
#include "base/macros.h"
#include <assert.h>

namespace fb {


class DelayedDestruction {
 public:
  class Destructor {
   public:
    void operator()(DelayedDestruction* dd) const {
      dd->Destroy();
    }
  };

  virtual void Destroy() {
    if (guard_count_ != 0) {
      destroy_pending_ = true;
    } else {
      DestroyNow(false);
    }
  }
  
  class DestructorGuard {
   public:
    explicit DestructorGuard(DelayedDestruction* dd) : dd_(dd) {
      ++dd_->guard_count_;
      assert(dd_->guard_count_ > 0);
    }

    DestructorGuard(const DestructorGuard& dg) : dd_(dg.dd_) {
      ++dd_->guard_count_;
      assert(dd_->guard_count_ > 0);
    }

    ~DestructorGuard() {
      assert(dd_->guard_ount_ > 0);
      --dd_->guard_count_;
      if (dd_->guard_count_ == 0 && dd_->destroy_pending_) {
        dd_->destroy_pending_ = false;
        dd_->DestroyNow(true);
      }
    }

   private:
    DelayedDestruction* dd_;
  };

 protected:
  virtual void DestroyNow(bool /* delayed */) {
    delete this;
  }

  DelayedDestruction()
      : guard_count_(0),
        destroy_pending_(false) {
  }

  virtual ~DelayedDestruction() {}
  uint32_t GetDestructorGuardCount() const {
    return guard_count_;
  }

 private:
  uint32_t guard_count_;
  bool destroy_pending_;

  DISALLOW_COPY_AND_ASSIGN(DelayedDestruction);
};

} // namespace fb

#endif // FB_DELAYED_DESTRUCTION_H_
