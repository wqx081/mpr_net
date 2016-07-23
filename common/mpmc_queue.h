#pragma once

#include <algorithm>
#include <atomic>
#include <assert.h>
#include <boost/noncopyable.hpp>
#include <limits>
#include <string.h>
#include <type_traits>
#include <unistd.h>
#include <stdexcept>

#include "base/macros.h"
#include "common/futex.h"


namespace fb {

namespace detail {

template<typename T, template<typename> class Atom>
struct SingleElementQueue;

template <typename T> class MPMCPipelineStageImpl;

} // namespace detail

template<typename T,
         template<typename> class Atom = std::atomic>
class MPMCQueue : boost::noncopyable {

static_assert(std::is_nothrow_constructible<T,T&&>::value ||
IsRelocatable<T>::value,
"T must be relocatable or have a noexcept move constructor");

  friend class detail::MPMCPipelineStageImpl<T>;
 public:
  typedef T value_type;

  explicit MPMCQueue(size_t queueCapacity)
    : capacity_(queueCapacity)
    , pushTicket_(0)
    , popTicket_(0)
    , pushSpinCutoff_(0)
    , popSpinCutoff_(0)
  {
    if (queueCapacity == 0)
      throw std::invalid_argument(
        "MPMCQueue with explicit capacity 0 is impossible"
      );

    // would sigfpe if capacity is 0
    stride_ = computeStride(queueCapacity);
    slots_ = new detail::SingleElementQueue<T,Atom>[queueCapacity +
                                                    2 * kSlotPadding];

#if 0
    // ideally this would be a static assert, but g++ doesn't allow it
    assert(alignof(MPMCQueue<T,Atom>)
           >= kFalseSharingRange);
    assert(static_cast<uint8_t*>(static_cast<void*>(&popTicket_))
           - static_cast<uint8_t*>(static_cast<void*>(&pushTicket_))
           >= kFalseSharingRange);
#endif
  }

  MPMCQueue() noexcept
    : capacity_(0)
    , slots_(nullptr)
    , stride_(0)
    , pushTicket_(0)
    , popTicket_(0)
    , pushSpinCutoff_(0)
    , popSpinCutoff_(0)
  {}

  MPMCQueue(MPMCQueue<T,Atom>&& rhs) noexcept
    : capacity_(rhs.capacity_)
    , slots_(rhs.slots_)
    , stride_(rhs.stride_)
    , pushTicket_(rhs.pushTicket_.load(std::memory_order_relaxed))
    , popTicket_(rhs.popTicket_.load(std::memory_order_relaxed))
    , pushSpinCutoff_(rhs.pushSpinCutoff_.load(std::memory_order_relaxed))
    , popSpinCutoff_(rhs.popSpinCutoff_.load(std::memory_order_relaxed))
  {
    rhs.capacity_ = 0;
    rhs.slots_ = nullptr;
    rhs.stride_ = 0;
    rhs.pushTicket_.store(0, std::memory_order_relaxed);
    rhs.popTicket_.store(0, std::memory_order_relaxed);
    rhs.pushSpinCutoff_.store(0, std::memory_order_relaxed);
    rhs.popSpinCutoff_.store(0, std::memory_order_relaxed);
  }

  MPMCQueue<T,Atom> const& operator= (MPMCQueue<T,Atom>&& rhs) {
    if (this != &rhs) {
      this->~MPMCQueue();
      new (this) MPMCQueue(std::move(rhs));
    }
    return *this;
  }

  ~MPMCQueue() {
    delete[] slots_;
  }

  ssize_t size() const noexcept {
    uint64_t pushes = pushTicket_.load(std::memory_order_acquire); // A
    uint64_t pops = popTicket_.load(std::memory_order_acquire); // B
    while (true) {
      uint64_t nextPushes = pushTicket_.load(std::memory_order_acquire); // C
      if (pushes == nextPushes) {
        // pushTicket_ didn't change from A (or the previous C) to C,
        // so we can linearize at B (or D)
        return pushes - pops;
      }
      pushes = nextPushes;
      uint64_t nextPops = popTicket_.load(std::memory_order_acquire); // D
      if (pops == nextPops) {
        // popTicket_ didn't chance from B (or the previous D), so we
        // can linearize at C
        return pushes - pops;
      }
      pops = nextPops;
    }
  }

  /// Returns true if there are no items available for dequeue
  bool isEmpty() const noexcept {
    return size() <= 0;
  }

  /// Returns true if there is currently no empty space to enqueue
  bool isFull() const noexcept {
    // careful with signed -> unsigned promotion, since size can be negative
    return size() >= static_cast<ssize_t>(capacity_);
  }

  /// Returns is a guess at size() for contexts that don't need a precise
  /// value, such as stats.
  ssize_t sizeGuess() const noexcept {
    return writeCount() - readCount();
  }

  /// Doesn't change
  size_t capacity() const noexcept {
    return capacity_;
  }

  /// Returns the total number of calls to blockingWrite or successful
  /// calls to write, including those blockingWrite calls that are
  /// currently blocking
  uint64_t writeCount() const noexcept {
    return pushTicket_.load(std::memory_order_acquire);
  }

  /// Returns the total number of calls to blockingRead or successful
  /// calls to read, including those blockingRead calls that are currently
  /// blocking
  uint64_t readCount() const noexcept {
    return popTicket_.load(std::memory_order_acquire);
  }

  /// Enqueues a T constructed from args, blocking until space is
  /// available.  Note that this method signature allows enqueue via
  /// move, if args is a T rvalue, via copy, if args is a T lvalue, or
  /// via emplacement if args is an initializer list that can be passed
  /// to a T constructor.
  template <typename ...Args>
  void blockingWrite(Args&&... args) noexcept {
    enqueueWithTicket(pushTicket_++, std::forward<Args>(args)...);
  }

  template <typename ...Args>
  bool write(Args&&... args) noexcept {
    uint64_t ticket;
    if (tryObtainReadyPushTicket(ticket)) {
      // we have pre-validated that the ticket won't block
      enqueueWithTicket(ticket, std::forward<Args>(args)...);
      return true;
    } else {
      return false;
    }
  }

  template <typename ...Args>
  bool writeIfNotFull(Args&&... args) noexcept {
    uint64_t ticket;
    if (tryObtainPromisedPushTicket(ticket)) {
      enqueueWithTicket(ticket, std::forward<Args>(args)...);
      return true;
    } else {
      return false;
    }
  }

  /// Moves a dequeued element onto elem, blocking until an element
  /// is available
  void blockingRead(T& elem) noexcept {
    dequeueWithTicket(popTicket_++, elem);
  }

  /// If an item can be dequeued with no blocking, does so and returns
  /// true, otherwise returns false.
  bool read(T& elem) noexcept {
    uint64_t ticket;
    if (tryObtainReadyPopTicket(ticket)) {
      // the ticket has been pre-validated to not block
      dequeueWithTicket(ticket, elem);
      return true;
    } else {
      return false;
    }
  }

  /// If the queue is not empty, dequeues and returns true, otherwise
  /// returns false.  If the matching write is still in progress then this
  /// method may block waiting for it.  If you don't rely on being able
  /// to dequeue (such as by counting completed write) then you should
  /// prefer read.
  bool readIfNotEmpty(T& elem) noexcept {
    uint64_t ticket;
    if (tryObtainPromisedPopTicket(ticket)) {
      // the matching enqueue already has a ticket, but might not be done
      dequeueWithTicket(ticket, elem);
      return true;
    } else {
      return false;
    }
  }

 private:
  enum {
    /// Once every kAdaptationFreq we will spin longer, to try to estimate
    /// the proper spin backoff
    kAdaptationFreq = 128,

    /// To avoid false sharing in slots_ with neighboring memory
    /// allocations, we pad it with this many SingleElementQueue-s at
    /// each end
    kSlotPadding = (kFalseSharingRange - 1)
        / sizeof(detail::SingleElementQueue<T,Atom>) + 1
  };

  /// The maximum number of items in the queue at once
  size_t AVOID_FALSE_SHARING capacity_;

  /// An array of capacity_ SingleElementQueue-s, each of which holds
  /// either 0 or 1 item.  We over-allocate by 2 * kSlotPadding and don't
  /// touch the slots at either end, to avoid false sharing
  detail::SingleElementQueue<T,Atom>* slots_;

  /// The number of slots_ indices that we advance for each ticket, to
  /// avoid false sharing.  Ideally slots_[i] and slots_[i + stride_]
  /// aren't on the same cache line
  int stride_;

  /// Enqueuers get tickets from here
  Atom<uint64_t> AVOID_FALSE_SHARING pushTicket_;

  /// Dequeuers get tickets from here
  Atom<uint64_t> AVOID_FALSE_SHARING popTicket_;

  /// This is how many times we will spin before using FUTEX_WAIT when
  /// the queue is full on enqueue, adaptively computed by occasionally
  /// spinning for longer and smoothing with an exponential moving average
  Atom<uint32_t> AVOID_FALSE_SHARING pushSpinCutoff_;

  /// The adaptive spin cutoff when the queue is empty on dequeue
  Atom<uint32_t> AVOID_FALSE_SHARING popSpinCutoff_;

  /// Alignment doesn't prevent false sharing at the end of the struct,
  /// so fill out the last cache line
  char padding_[kFalseSharingRange - sizeof(Atom<uint32_t>)];

  static int computeStride(size_t capacity) noexcept {
    static const int smallPrimes[] = { 2, 3, 5, 7, 11, 13, 17, 19, 23 };

    int bestStride = 1;
    size_t bestSep = 1;
    for (int stride : smallPrimes) {
      if ((stride % capacity) == 0 || (capacity % stride) == 0) {
        continue;
      }
      size_t sep = stride % capacity;
      sep = std::min(sep, capacity - sep);
      if (sep > bestSep) {
        bestStride = stride;
        bestSep = sep;
      }
    }
    return bestStride;
  }

  size_t idx(uint64_t ticket) noexcept {
    return ((ticket * stride_) % capacity_) + kSlotPadding;
  }

  uint32_t turn(uint64_t ticket) noexcept {
    return ticket / capacity_;
  }

  bool tryObtainReadyPushTicket(uint64_t& rv) noexcept {
    auto ticket = pushTicket_.load(std::memory_order_acquire); // A
    while (true) {
      if (!slots_[idx(ticket)].mayEnqueue(turn(ticket))) {
        auto prev = ticket;
        ticket = pushTicket_.load(std::memory_order_acquire); // B
        if (prev == ticket) {
          return false;
        }
      } else {
        if (pushTicket_.compare_exchange_strong(ticket, ticket + 1)) {
          rv = ticket;
          return true;
        }
      }
    }
  }

  bool tryObtainPromisedPushTicket(uint64_t& rv) noexcept {
    auto numPushes = pushTicket_.load(std::memory_order_acquire); // A
    while (true) {
      auto numPops = popTicket_.load(std::memory_order_acquire); // B
      // n will be negative if pops are pending
      int64_t n = numPushes - numPops;
      if (n >= static_cast<ssize_t>(capacity_)) {
        // Full, linearize at B.  We don't need to recheck the read we
        // performed at A, because if numPushes was stale at B then the
        // real numPushes value is even worse
        return false;
      }
      if (pushTicket_.compare_exchange_strong(numPushes, numPushes + 1)) {
        rv = numPushes;
        return true;
      }
    }
  }

  /// Tries to obtain a pop ticket for which SingleElementQueue::dequeue
  /// won't block.  Returns true on immediate success, false on immediate
  /// failure.
  bool tryObtainReadyPopTicket(uint64_t& rv) noexcept {
    auto ticket = popTicket_.load(std::memory_order_acquire);
    while (true) {
      if (!slots_[idx(ticket)].mayDequeue(turn(ticket))) {
        auto prev = ticket;
        ticket = popTicket_.load(std::memory_order_acquire);
        if (prev == ticket) {
          return false;
        }
      } else {
        if (popTicket_.compare_exchange_strong(ticket, ticket + 1)) {
          rv = ticket;
          return true;
        }
      }
    }
  }

  bool tryObtainPromisedPopTicket(uint64_t& rv) noexcept {
    auto numPops = popTicket_.load(std::memory_order_acquire); // A
    while (true) {
      auto numPushes = pushTicket_.load(std::memory_order_acquire); // B
      if (numPops >= numPushes) {
        return false;
      }
      if (popTicket_.compare_exchange_strong(numPops, numPops + 1)) {
        rv = numPops;
        return true;
      }
    }
  }

  // Given a ticket, constructs an enqueued item using args
  template <typename ...Args>
  void enqueueWithTicket(uint64_t ticket, Args&&... args) noexcept {
    slots_[idx(ticket)].enqueue(turn(ticket),
                                pushSpinCutoff_,
                                (ticket % kAdaptationFreq) == 0,
                                std::forward<Args>(args)...);
  }

  // Given a ticket, dequeues the corresponding element
  void dequeueWithTicket(uint64_t ticket, T& elem) noexcept {
    slots_[idx(ticket)].dequeue(turn(ticket),
                                popSpinCutoff_,
                                (ticket % kAdaptationFreq) == 0,
                                elem);
  }
};


namespace detail {

template <template<typename> class Atom>
struct TurnSequencer {
  explicit TurnSequencer(const uint32_t firstTurn = 0) noexcept
      : state_(encode(firstTurn << kTurnShift, 0))
  {}

  /// Returns true iff a call to waitForTurn(turn, ...) won't block
  bool isTurn(const uint32_t turn) const noexcept {
    auto state = state_.load(std::memory_order_acquire);
    return decodeCurrentSturn(state) == (turn << kTurnShift);
  }

  void waitForTurn(const uint32_t turn,
                   Atom<uint32_t>& spinCutoff,
                   const bool updateSpinCutoff) noexcept {
    uint32_t prevThresh = spinCutoff.load(std::memory_order_relaxed);
    const uint32_t effectiveSpinCutoff =
        updateSpinCutoff || prevThresh == 0 ? kMaxSpins : prevThresh;

    uint32_t tries;
    const uint32_t sturn = turn << kTurnShift;
    for (tries = 0; ; ++tries) {
      uint32_t state = state_.load(std::memory_order_acquire);
      uint32_t current_sturn = decodeCurrentSturn(state);
      if (current_sturn == sturn) {
        break;
      }

      // wrap-safe version of assert(current_sturn < sturn)
      assert(sturn - current_sturn < std::numeric_limits<uint32_t>::max() / 2);

      // the first effectSpinCutoff tries are spins, after that we will
      // record ourself as a waiter and block with futexWait
      if (tries < effectiveSpinCutoff) {
        asm volatile ("pause");
        continue;
      }

      uint32_t current_max_waiter_delta = decodeMaxWaitersDelta(state);
      uint32_t our_waiter_delta = (sturn - current_sturn) >> kTurnShift;
      uint32_t new_state;
      if (our_waiter_delta <= current_max_waiter_delta) {
        // state already records us as waiters, probably because this
        // isn't our first time around this loop
        new_state = state;
      } else {
        new_state = encode(current_sturn, our_waiter_delta);
        if (state != new_state &&
            !state_.compare_exchange_strong(state, new_state)) {
          continue;
        }
      }
      state_.futexWait(new_state, futexChannel(turn));
    }

    if (updateSpinCutoff || prevThresh == 0) {
      // if we hit kMaxSpins then spinning was pointless, so the right
      // spinCutoff is kMinSpins
      uint32_t target;
      if (tries >= kMaxSpins) {
        target = kMinSpins;
      } else {
        // to account for variations, we allow ourself to spin 2*N when
        // we think that N is actually required in order to succeed
        target = std::min<uint32_t>(kMaxSpins,
                                    std::max<uint32_t>(kMinSpins, tries * 2));
      }

      if (prevThresh == 0) {
        // bootstrap
        spinCutoff.store(target);
      } else {
        // try once, keep moving if CAS fails.  Exponential moving average
        // with alpha of 7/8
        // Be careful that the quantity we add to prevThresh is signed.
        spinCutoff.compare_exchange_weak(
            prevThresh, prevThresh + int(target - prevThresh) / 8);
      }
    }
  }

  /// Unblocks a thread running waitForTurn(turn + 1)
  void completeTurn(const uint32_t turn) noexcept {
    uint32_t state = state_.load(std::memory_order_acquire);
    while (true) {
      assert(state == encode(turn << kTurnShift, decodeMaxWaitersDelta(state)));
      uint32_t max_waiter_delta = decodeMaxWaitersDelta(state);
      uint32_t new_state = encode(
              (turn + 1) << kTurnShift,
              max_waiter_delta == 0 ? 0 : max_waiter_delta - 1);
      if (state_.compare_exchange_strong(state, new_state)) {
        if (max_waiter_delta != 0) {
          state_.futexWake(std::numeric_limits<int>::max(),
                           futexChannel(turn + 1));
        }
        break;
      }
      // failing compare_exchange_strong updates first arg to the value
      // that caused the failure, so no need to reread state_
    }
  }

  /// Returns the least-most significant byte of the current uncompleted
  /// turn.  The full 32 bit turn cannot be recovered.
  uint8_t uncompletedTurnLSB() const noexcept {
    return state_.load(std::memory_order_acquire) >> kTurnShift;
  }

 private:
  enum : uint32_t {
    /// kTurnShift counts the bits that are stolen to record the delta
    /// between the current turn and the maximum waiter. It needs to be big
    /// enough to record wait deltas of 0 to 32 inclusive.  Waiters more
    /// than 32 in the future will be woken up 32*n turns early (since
    /// their BITSET will hit) and will adjust the waiter count again.
    /// We go a bit beyond and let the waiter count go up to 63, which
    /// is free and might save us a few CAS
    kTurnShift = 6,
    kWaitersMask = (1 << kTurnShift) - 1,

    /// The minimum spin count that we will adaptively select
    kMinSpins = 20,

    /// The maximum spin count that we will adaptively select, and the
    /// spin count that will be used when probing to get a new data point
    /// for the adaptation
    kMaxSpins = 2000,
  };

  /// This holds both the current turn, and the highest waiting turn,
  /// stored as (current_turn << 6) | min(63, max(waited_turn - current_turn))
  Futex<Atom> state_;

  /// Returns the bitmask to pass futexWait or futexWake when communicating
  /// about the specified turn
  int futexChannel(uint32_t turn) const noexcept {
    return 1 << (turn & 31);
  }

  uint32_t decodeCurrentSturn(uint32_t state) const noexcept {
    return state & ~kWaitersMask;
  }

  uint32_t decodeMaxWaitersDelta(uint32_t state) const noexcept {
    return state & kWaitersMask;
  }

  uint32_t encode(uint32_t currentSturn, uint32_t maxWaiterD) const noexcept {
    return currentSturn | std::min(uint32_t{ kWaitersMask }, maxWaiterD);
  }
};


/// SingleElementQueue implements a blocking queue that holds at most one
/// item, and that requires its users to assign incrementing identifiers
/// (turns) to each enqueue and dequeue operation.  Note that the turns
/// used by SingleElementQueue are doubled inside the TurnSequencer
template <typename T, template <typename> class Atom>
struct SingleElementQueue {

  ~SingleElementQueue() noexcept {
    if ((sequencer_.uncompletedTurnLSB() & 1) == 1) {
      // we are pending a dequeue, so we have a constructed item
      destroyContents();
    }
  }

  /// enqueue using in-place noexcept construction
  template <typename ...Args,
            typename = typename std::enable_if<
                std::is_nothrow_constructible<T,Args...>::value>::type>
  void enqueue(const uint32_t turn,
               Atom<uint32_t>& spinCutoff,
               const bool updateSpinCutoff,
               Args&&... args) noexcept {
    sequencer_.waitForTurn(turn * 2, spinCutoff, updateSpinCutoff);
    new (&contents_) T(std::forward<Args>(args)...);
    sequencer_.completeTurn(turn * 2);
  }

  /// enqueue using move construction, either real (if
  /// is_nothrow_move_constructible) or simulated using relocation and
  /// default construction (if IsRelocatable and has_nothrow_constructor)
  template <typename = typename std::enable_if<
               (IsRelocatable<T>::value &&
                boost::has_nothrow_constructor<T>::value) ||
                std::is_nothrow_constructible<T,T&&>::value>::type>
  void enqueue(const uint32_t turn,
               Atom<uint32_t>& spinCutoff,
               const bool updateSpinCutoff,
               T&& goner) noexcept {
    if (std::is_nothrow_constructible<T,T&&>::value) {
      // this is preferred
      sequencer_.waitForTurn(turn * 2, spinCutoff, updateSpinCutoff);
      new (&contents_) T(std::move(goner));
      sequencer_.completeTurn(turn * 2);
    } else {
      // simulate nothrow move with relocation, followed by default
      // construction to fill the gap we created
      sequencer_.waitForTurn(turn * 2, spinCutoff, updateSpinCutoff);
      memcpy(&contents_, &goner, sizeof(T));
      sequencer_.completeTurn(turn * 2);
      new (&goner) T();
    }
  }

  bool mayEnqueue(const uint32_t turn) const noexcept {
    return sequencer_.isTurn(turn * 2);
  }

  void dequeue(uint32_t turn,
               Atom<uint32_t>& spinCutoff,
               const bool updateSpinCutoff,
               T& elem) noexcept {
    if (IsRelocatable<T>::value) {
      // this version is preferred, because we do as much work as possible
      // before waiting
      try {
        elem.~T();
      } catch (...) {
        // unlikely, but if we don't complete our turn the queue will die
      }
      sequencer_.waitForTurn(turn * 2 + 1, spinCutoff, updateSpinCutoff);
      memcpy(&elem, &contents_, sizeof(T));
      sequencer_.completeTurn(turn * 2 + 1);
    } else {
      // use nothrow move assignment
      sequencer_.waitForTurn(turn * 2 + 1, spinCutoff, updateSpinCutoff);
      elem = std::move(*ptr());
      destroyContents();
      sequencer_.completeTurn(turn * 2 + 1);
    }
  }

  bool mayDequeue(const uint32_t turn) const noexcept {
    return sequencer_.isTurn(turn * 2 + 1);
  }

 private:
  /// Storage for a T constructed with placement new
  typename std::aligned_storage<sizeof(T),alignof(T)>::type contents_;

  /// Even turns are pushes, odd turns are pops
  TurnSequencer<Atom> sequencer_;

  T* ptr() noexcept {
    return static_cast<T*>(static_cast<void*>(&contents_));
  }

  void destroyContents() noexcept {
    try {
      ptr()->~T();
    } catch (...) {
      // g++ doesn't seem to have std::is_nothrow_destructible yet
    }
#ifndef NDEBUG
    memset(&contents_, 'Q', sizeof(T));
#endif
  }
};

} // namespace detail

} // namespace 
