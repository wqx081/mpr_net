#ifndef FB_EXECUTOR_H_
#define FB_EXECUTOR_H_
#include <atomic>
#include <functional>

namespace fb {

typedef std::function<void()> Func;

class Executor {
 public:
  virtual ~Executor() {}
  virtual void Add(Func) = 0;

  template<typename P>
  void AddPtr(P fn) {
    this->Add([fn]() mutable { (*fn)(); });
  }  
};

} // namespace fb
#endif // FB_EXECUTOR_H_
