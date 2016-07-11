#include "net/rate_limiter.h"

namespace net {

bool RateLimiter::CanUse(size_t desired, double time) {
  return ((time > period_end_ && desired <= max_per_period_) ||
          (used_in_period_ + desired) <= max_per_period_);
}

void RateLimiter::Use(size_t used, double time) {
  if (time > period_end_) {
    period_start_ = time;
    period_end_ = time + period_length_;
    used_in_period_ = 0;
  }
  used_in_period_ += used;
}

} // namespace net
