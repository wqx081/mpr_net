#ifndef TASK_TASK_PARENT_H_
#define TASK_TASK_PARENT_H_
#include <memory>
#include <set>

#include "base/macros.h"

namespace net {

class Task;
class TaskRunner;

class TaskParent {
 public:
  TaskParent(Task *derived_instance, TaskParent *parent);
  explicit TaskParent(TaskRunner *derived_instance);
  virtual ~TaskParent();

  TaskParent* GetParent() { return parent_; }
  TaskRunner* GetRunner() { return runner_; }

  bool AllChildrenDone();
  bool AnyChildError();
#if !defined(NDEBUG)
  bool IsChildTask(Task* task);
#endif

 protected:
  void OnStopped(Task* task);
  void AbortAllChildren();
  TaskParent* parent() {
    return parent_;
  }

 private:
  void Initialize();
  void OnChildStopped(Task* child);
  void AddChild(Task *child);

  TaskParent* parent_;
  TaskRunner* runner_;
  bool child_error_;
  typedef std::set<Task *> ChildSet;
  std::unique_ptr<ChildSet> children_;
  DISALLOW_COPY_AND_ASSIGN(TaskParent);
};

} // namespace net
#endif // TASK_TASK_PARENT_H_
