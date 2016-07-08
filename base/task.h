#ifndef BASE_TASK_H_
#define BASE_TASK_H_

#include <stddef.h>
#include <stdint.h>

namespace base {

/**
 * A Task represents a unit of work.
 */
class Task {
 public:
  virtual ~Task() {}

  virtual void Run() = 0;
};

/**
 * An IdleTask represents a unit of work to be performed in idle time.
 * The Run method is invoked with an argument that specifies the deadline.
 */
class IdleTask {
 public:
  virtual ~IdleTask() {}
  virtual void Run(double deadline_in_seconds) = 0;
};


} // namespace base
#endif // BASE_TASK_H_
