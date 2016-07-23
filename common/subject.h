#pragma once

#include "common/types.h"
#include "common/observable.h"
#include "common/observer.h"

namespace fb {

template<class T>
struct Subject : public Observable<T>,
                 public Observer<T> {
 void OnNext(const T& val) override {
   this->ForEachObserver([&](Observer<T>* o) {
     o->OnNext(val);		   
   });
 }
 void OnError(Error e) override {
   this->ForEachObserver([&](Observer<T>* o) {
     o->OnError(e);
   });
 }
 void OnCompleted() override {
   this->ForEachObserver([](Observer<T>* o) {
     o->OnCompleted();		   
   });
 }
};

} // namespace fb
