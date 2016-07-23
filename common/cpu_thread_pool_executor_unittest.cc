#include "common/thread_pool_executor.h"
#include "common/cpu_thread_pool_executor.h"

#include <gtest/gtest.h>

using namespace fb;
using namespace std::chrono;

static Func BurmMs(uint64_t ms) {
  return [ms]() { std::this_thread::sleep_for(milliseconds(ms)); };
}

template<class TPE>
static void Basic() {
  TPE tpe(10);
}

TEST(ThreadPoolExecutorTest, CPUBasic) {
  Basic<CPUThreadPoolExecutor>();
}

template<typename TPE>
static void Resize() {
  TPE tpe(100);
  EXPECT_EQ(100, tpe.num_threads());
  tpe.SetNumThreads(50);
  EXPECT_EQ(50, tpe.num_threads());
  tpe.SetNumThreads(150);
  EXPECT_EQ(150, tpe.num_threads());
}

TEST(ThreadPoolExecutorTest, CPUResize) {
  Resize<CPUThreadPoolExecutor>();
}

template<typename TPE>
static void Stop() {
  TPE tpe(1);
  std::atomic<int> completed(0);
  auto f = [&]() {
    BurmMs(10)();
    completed++;
  };
  for (int i = 0; i < 1000; ++i) {
    tpe.Add(f);
  }
  tpe.Stop();
  EXPECT_GT(1000, completed);
}

TEST(ThreadPoolExecutorTest, CPUStop) {
 Stop<CPUThreadPoolExecutor>();
}

template<typename TPE>
static void Join() {
  TPE tpe(10);
  std::atomic<int> completed(0);
  auto f = [&]() {
    BurmMs(1)();
    completed++;
  };
  for (int i = 0; i < 1000; ++i) {
    tpe.Add(f);
  }
  tpe.Join();
  EXPECT_EQ(1000, completed);
}

TEST(ThreadPoolExecutorTest, CPUJoin) {
  Join<CPUThreadPoolExecutor>();
}

template<typename TPE>
static void ResizeUnderLoad() {
  TPE tpe(10);
  std::atomic<int> completed(0);
  auto f = [&]() {
    BurmMs(1)();
    completed++;
  };
  for (int i = 0; i < 1000; ++i) {
    tpe.Add(f);
  }
  tpe.SetNumThreads(5);
  tpe.SetNumThreads(15);
  tpe.Join();
  EXPECT_EQ(1000, completed);
}

TEST(ThreadPoolExecutorTest, CPUResizeUnderLoad) {
  ResizeUnderLoad<CPUThreadPoolExecutor>();
}

template<typename TPE>
static void PoolStats() {
  Baton<> start_baton, end_baton;
  TPE tpe(1);
  auto stats = tpe.GetPoolStats();
  EXPECT_EQ(1, stats.thread_count);
  EXPECT_EQ(1, stats.idle_thread_count);
  EXPECT_EQ(0, stats.active_thread_count);
  EXPECT_EQ(0, stats.pending_task_count);
  EXPECT_EQ(0, stats.total_task_count);
  tpe.Add([&]() { start_baton.post(); end_baton.wait(); });
  tpe.Add([&]() {});
  start_baton.wait();
  stats = tpe.GetPoolStats();
  EXPECT_EQ(1, stats.thread_count);
  EXPECT_EQ(0, stats.idle_thread_count);
  EXPECT_EQ(1, stats.active_thread_count);
  EXPECT_EQ(1, stats.pending_task_count);
  EXPECT_EQ(2, stats.total_task_count);
  end_baton.post();
}

TEST(ThreadPoolExecutorTest, CPUPoolStats) {
  PoolStats<CPUThreadPoolExecutor>();
}

template<typename TPE>
static void TaskStats() {
  TPE tpe(1);
  std::atomic<int> c(0);
  auto s = tpe.SubscribeToTaskStats(
     Observer<CPUThreadPoolExecutor::TaskStats>::Create(
       [&](CPUThreadPoolExecutor::TaskStats stats) {
         int i = c++;
	 EXPECT_LT(milliseconds(0), stats.run_time);
	 if (i == 1) {
	   EXPECT_LT(milliseconds(0), stats.wait_time);
	 }
       }));
  tpe.Add(BurmMs(10));
  tpe.Add(BurmMs(10));
  tpe.Join();
  EXPECT_EQ(2, c);
}

TEST(ThreadPoolExecutorTest, CPUTaskStats) {
  TaskStats<CPUThreadPoolExecutor>();
}

template<typename TPE>
static void Expiration() {
  TPE tpe(1);
  std::atomic<int> stat_cb_count(0);
  auto s = tpe.SubscribeToTaskStats(
    Observer<CPUThreadPoolExecutor::TaskStats>::Create(
      [&](CPUThreadPoolExecutor::TaskStats stats) {
        int i = stat_cb_count++;
	if (i == 0) {
	  EXPECT_FALSE(stats.expired);
	} else if (i == 1) {
	  EXPECT_TRUE(stats.expired);
	} else {
	  FAIL();
	}
      }
  ));

  std::atomic<int> expire_cb_count(0);
  auto expire_cb = [&]() { expire_cb_count++; };
  tpe.Add(BurmMs(10), seconds(60), expire_cb);
  tpe.Add(BurmMs(10), milliseconds(10), expire_cb);
  tpe.Join();
  EXPECT_EQ(2, stat_cb_count);
  EXPECT_EQ(1, expire_cb_count);
}

TEST(ThreadPoolExecutorTest, CPUExpiration) {
  Expiration<CPUThreadPoolExecutor>();
}

TEST(ThreadPoolExecutorTest, PriorityPreemptionTest) {
  bool tookLopri = false;
  auto completed = 0;
  auto hipri = [&] {
    EXPECT_FALSE(tookLopri);
    completed++;
  };
  auto lopri = [&] {
    tookLopri = true;
    completed++;
  };
  CPUThreadPoolExecutor pool(0, 2);
  for (int i = 0; i < 50; i++) {
    pool.Add(lopri, 0);
  }
  for (int i = 0; i < 50; i++) {
    pool.Add(hipri, 1);
  }
  pool.SetNumThreads(1);
  pool.Join();
  EXPECT_EQ(100, completed);
}


class TestObserver : public CPUThreadPoolExecutor::Observer {
 public:
  void ThreadStarted(CPUThreadPoolExecutor::ThreadHandle*) {
    threads_++;
  }
  void ThreadStopped(CPUThreadPoolExecutor::ThreadHandle*) {
    threads_--;
  }
  void ThreadPreviouslyStarted(CPUThreadPoolExecutor::ThreadHandle*) {
    threads_++;
  }
  void ThreadNotYetStopped(CPUThreadPoolExecutor::ThreadHandle*) {
    threads_--;
  }
  void CheckCalls() {
    ASSERT_EQ(threads_, 0);
  }

 private:
  std::atomic<int> threads_{0};
};

TEST(ThreadPoolExecutorTest, IOObserver) {
  auto observer = std::make_shared<TestObserver>();
  {
    CPUThreadPoolExecutor exe(10);
    exe.AddObserver(observer);
    exe.SetNumThreads(3);
    exe.SetNumThreads(0);
    exe.SetNumThreads(7);
    exe.RemoveObserver(observer);
    exe.SetNumThreads(10);
  }
  observer->CheckCalls();
}
