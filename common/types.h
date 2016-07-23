#pragma once

#include "fb/executor.h"
#include <memory>

namespace fb {
  typedef std::exception Error;
  typedef std::shared_ptr<Executor> SchedulerPtr;

  template <class T, size_t InlineObservers = 3> struct Observable;
  template <class T> struct Observer;
  template <class T> struct Subject;

  template <typename T> using ObservablePtr = std::shared_ptr<Observable<T>>;
  template <class T> using ObserverPtr = std::shared_ptr<Observer<T>>;
  template <class T> using SubjectPtr = std::shared_ptr<Subject<T>>;

} // namespace fb
