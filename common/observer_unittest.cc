#include "common/observer.h"
#include "common/subject.h"

#include <gtest/gtest.h>

using namespace fb;

static std::unique_ptr<Observer<int>> Incrementer(int& counter) {
  return Observer<int>::Create([&](int x) {
    (void)x;
    counter++;
  });
}

TEST(Observer, Observe) {
  Subject<int> subject;
  auto count = 0;
  subject.Observe(Incrementer(count));
  subject.OnNext(1);
  EXPECT_EQ(1, count);
}

TEST(Observer, ObserveInline) {
  Subject<int> subject;
  auto count = 0;
  auto o = Incrementer(count).release();
  subject.Observe(o);
  subject.OnNext(1);
  EXPECT_EQ(1, count);
  delete o;
}

TEST(Observer, Subscription) {
  Subject<int> subject;
  auto count = 0;
  {
    auto s = subject.Subscribe(Incrementer(count));
    subject.OnNext(1);
  }
  subject.OnNext(2);
  EXPECT_EQ(1, count);
}

//TODO(wqx):
