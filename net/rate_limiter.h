#ifndef NET_RATE_LIMITER_H_
#define NET_RATE_LIMITER_H_
#include <stdlib.h>

namespace net {

class RateLimiter {
 public:
  RateLimiter(size_t max, double period)
      : max_per_period_(max),
	period_length_(period),
	used_in_period_(0),
	period_start_(0.0),
	period_end_(period) {
  }
  virtual ~RateLimiter() {}

  bool CanUse(size_t desired, double time);
  void Use(size_t used, double time);
  size_t used_in_period() const {
    return used_in_period_;
  }
  size_t max_per_period() const {
    return max_per_period_;
  }

 private:
  size_t max_per_period_;
  double period_length_;
  size_t used_in_period_;
  double period_start_;
  double period_end_;
};

} // namespace net
#endif // NET_RATE_LIMITER_H_
