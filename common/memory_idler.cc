#include "common/memory_idler.h"
#include "fb/scope_guard.h"

#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <utility>


namespace fb { namespace detail {

AtomicStruct<std::chrono::steady_clock::duration>
MemoryIdler::defaultIdleTimeout(std::chrono::seconds(5));


void MemoryIdler::flushLocalMallocCaches() {
}


static const size_t s_pageSize = sysconf(_SC_PAGESIZE);
static __thread uintptr_t tls_stackLimit;
static __thread size_t tls_stackSize;

static void fetchStackLimits() {
  pthread_attr_t attr;
//#if defined(_GNU_SOURCE) && defined(__linux__) // Linux+GNU extension
  pthread_getattr_np(pthread_self(), &attr);
//#else
//  pthread_attr_init(&attr);
//#endif
  SCOPE_EXIT { pthread_attr_destroy(&attr); };

  void* addr;
  size_t rawSize;
  int err;
  if ((err = pthread_attr_getstack(&attr, &addr, &rawSize))) {
    // unexpected, but it is better to continue in prod than do nothing
    LOG(ERROR) << "pthread_attr_getstack error " << err;
    assert(false);
    tls_stackSize = 1;
    return;
  }
  assert(addr != nullptr);
  assert(rawSize >= PTHREAD_STACK_MIN);

  // glibc subtracts guard page from stack size, even though pthread docs
  // seem to imply the opposite
  size_t guardSize;
  if (pthread_attr_getguardsize(&attr, &guardSize) != 0) {
    guardSize = 0;
  }
  assert(rawSize > guardSize);

  // stack goes down, so guard page adds to the base addr
  tls_stackLimit = uintptr_t(addr) + guardSize;
  tls_stackSize = rawSize - guardSize;

  assert((tls_stackLimit & (s_pageSize - 1)) == 0);
}

inline static uintptr_t getStackPtr() {
  char marker;
  auto rv = uintptr_t(&marker);
  return rv;
}

void MemoryIdler::unmapUnusedStack(size_t retain) {
  if (tls_stackSize == 0) {
    fetchStackLimits();
  }
  if (tls_stackSize <= std::max(size_t(1), retain)) {
    // covers both missing stack info, and impossibly large retain
    return;
  }

  auto sp = getStackPtr();
  assert(sp >= tls_stackLimit);
  assert(sp - tls_stackLimit < tls_stackSize);

  auto end = (sp - retain) & ~(s_pageSize - 1);
  if (end <= tls_stackLimit) {
    // no pages are eligible for unmapping
    return;
  }

  size_t len = end - tls_stackLimit;
  assert((len & (s_pageSize - 1)) == 0);
  if (madvise((void*)tls_stackLimit, len, MADV_DONTNEED) != 0) {
    // It is likely that the stack vma hasn't been fully grown.  In this
    // case madvise will apply dontneed to the present vmas, then return
    // errno of ENOMEM.  We can also get an EAGAIN, theoretically.
    // EINVAL means either an invalid alignment or length, or that some
    // of the pages are locked or shared.  Neither should occur.
    assert(errno == EAGAIN || errno == ENOMEM);
  }
}

}}
