#pragma once

#include <cstdlib>
#include <type_traits>
#include <utility>
#include <cassert>

namespace fb {

/**
 * A helper class to allow a DelayedDestruction object to be instantiated on
 * the stack.
 *
 * This class derives from an existing DelayedDestruction type and makes the
 * destructor public again.  This allows objects of this type to be declared on
 * the stack or directly inside another class.  Normally DelayedDestruction
 * objects must be dynamically allocated on the heap.
 *
 * However, the trade-off is that you lose some of the protections provided by
 * DelayedDestruction::destroy().  DelayedDestruction::destroy() will
 * automatically delay destruction of the object until it is safe to do so.
 * If you use UndelayedDestruction, you become responsible for ensuring that
 * you only destroy the object where it is safe to do so.  Attempting to
 * destroy a UndelayedDestruction object while it has a non-zero destructor
 * guard count will abort the program.
 */
template<typename TDD>
class UndelayedDestruction : public TDD {
 public:
  // We could just use constructor inheritance, but not all compilers
  // support that. So, just use a forwarding constructor.
  //
  // Ideally we would use std::enable_if<> and std::is_constructible<> to
  // provide only constructor methods that are valid for our parent class.
  // Unfortunately std::is_constructible<> doesn't work for types that aren't
  // destructible.  In gcc-4.6 it results in a compiler error.  In the latest
  // gcc code it looks like it has been fixed to return false.  (The language
  // in the standard seems to indicate that returning false is the correct
  // behavior for non-destructible types, which is unfortunate.)
  template<typename ...Args>
  explicit UndelayedDestruction(Args&& ...args)
    : TDD(std::forward<Args>(args)...) {}

  /**
   * Public destructor.
   *
   * The caller is responsible for ensuring that the object is only destroyed
   * where it is safe to do so.  (i.e., when the destructor guard count is 0).
   *
   * The exact conditions for meeting this may be dependant upon your class
   * semantics.  Typically you are only guaranteed that it is safe to destroy
   * the object directly from the event loop (e.g., directly from a
   * TEventBase::LoopCallback), or when the event loop is stopped.
   */
  virtual ~UndelayedDestruction() {
    // Crash if the caller is destroying us with outstanding destructor guards.
    if (this->GetDestructorGuardCount() != 0) {
      abort();
    }
    // Invoke destroy.  This is necessary since our base class may have
    // implemented custom behavior in destroy().
    this->Destroy();
  }

 protected:
  /**
   * Override our parent's destroy() method to make it protected.
   * Callers should use the normal destructor instead of destroy
   */
  virtual void Destroy() {
    this->TDD::Destroy();
  }

  virtual void DestroyNow(bool delayed) {
    // Do nothing.  This will always be invoked from the call to destroy inside
    // our destructor.
    assert(!delayed);
    // prevent unused variable warnings when asserts are compiled out.
    (void)delayed;
  }

 private:
  // Forbidden copy constructor and assignment operator
  UndelayedDestruction(UndelayedDestruction const &) = delete;
  UndelayedDestruction& operator=(UndelayedDestruction const &) = delete;
};

}
