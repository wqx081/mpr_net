// Example usage:
//   static LazyInstance<MyClass>::type my_instance = LAZY_INSTANCE_INITIALIZER;
//   void SomeMethod() {
//     my_instance.Get().SomeMethod();  // MyClass::SomeMethod()
//
//     MyClass* ptr = my_instance.Pointer();
//     ptr->DoDoDo();  // MyClass::DoDoDo
//   }
// Additionally you can override the way your instance is constructed by
// providing your own trait:
//
// Example usage:
//   struct MyCreateTrait {
//     static void Construct(MyClass* allocated_ptr) {
//       new (allocated_ptr) MyClass(/* extra parameters... */);
//     }
//   };
//   static LazyInstance<MyClass, MyCreateTrait>::type my_instance = LAZY_INSTANCE_INITIALIZER;
//


#ifndef BASE_LAZY_INSTANCE_H_
#define BASE_LAZY_INSTANCE_H_
#include "base/macros.h"
#include "base/once.h"

namespace base {

#define LAZY_STATIC_INSTANCE_INITIALIZER { MPR_ONCE_INIT, { {} } }
#define LAZY_DYNAMIC_INSTANCE_INITIALIZER { MPR_ONCE_INIT, 0 }

#define LAZY_INSTANCE_INITIALIZER LAZY_STATIC_INSTANCE_INITIALIZER


template<typename T>
struct LeakyInstanceTrait {
  static void Destroy(T* /* instance */) {
  }
};

// Traits that define how an instance is allocated and accessed.


template<typename T>
struct StaticallyAllocatedInstanceTrait {
  struct MPR_ALIGNAS(T, 16) StorageType {
    char x[sizeof(T)];
  };

  STATIC_ASSERT(MPR_ALIGNOF(StorageType) >= MPR_ALIGNOF(T));

  static T* MutableInstance(StorageType* storage) {
    return reinterpret_cast<T*>(storage);
  }

  template<typename ConstructTrait>
  static void InitStorageUsingTrait(StorageType* storage) {
    ConstructTrait::Construct(MutableInstance(storage));
  }
};

template<typename T>
struct DynamicallyAllocatedInstanceTrait {
  typedef T* StorageType;

  static T* MutableInstance(StorageType* storage) {
    return *storage;
  }

  template<typename CreateTrait>
  static void InitStorageUsingTrait(StorageType* storage) {
    *storage = CreateTrait::Create();
  }
};

template<typename T>
struct DefaultConstructTrait {
  // Constructs the provided object which was already allocated.
  static void Construct(T* allocated_ptr) {
    new(allocated_ptr) T();
  }
};

template <typename T>
struct DefaultCreateTrait {
  static T* Create() {
    return new T();
  }
};


struct ThreadSafeInitOnceTrait {
  template <typename Function, typename Storage>
  static void Init(OnceType* once, Function function, Storage storage) {
    CallOnce(once, function, storage);
  }
};

struct SingleThreadInitOnceTrait {
  template <typename Function, typename Storage>
  static void Init(OnceType* once, Function function, Storage storage) {
    if (*once == ONCE_STATE_UNINITIALIZED) {
      function(storage);
      *once = ONCE_STATE_DONE;
    }
  }
};

template <typename T, typename AllocationTrait, typename CreateTrait,
          typename InitOnceTrait, typename DestroyTrait  /* not used yet. */>
struct LazyInstanceImpl {
 public:
  typedef typename AllocationTrait::StorageType StorageType;

 private:
  static void InitInstance(StorageType* storage) {
    AllocationTrait::template InitStorageUsingTrait<CreateTrait>(storage);
  }

  void Init() const {
    InitOnceTrait::Init(
        &once_,
        reinterpret_cast<void(*)(void*)>(&InitInstance),  // NOLINT
        reinterpret_cast<void*>(&storage_));
  }

 public:
  T* Pointer() {
    Init();
    return AllocationTrait::MutableInstance(&storage_);
  }

  const T& Get() const {
    Init();
    return *AllocationTrait::MutableInstance(&storage_);
  }

  mutable OnceType once_;
  mutable StorageType storage_;
};

template <typename T,
          typename CreateTrait = DefaultConstructTrait<T>,
          typename InitOnceTrait = ThreadSafeInitOnceTrait,
          typename DestroyTrait = LeakyInstanceTrait<T> >
struct LazyStaticInstance {
  typedef LazyInstanceImpl<T, StaticallyAllocatedInstanceTrait<T>,
      CreateTrait, InitOnceTrait, DestroyTrait> type;
};


template <typename T,
          typename CreateTrait = DefaultConstructTrait<T>,
          typename InitOnceTrait = ThreadSafeInitOnceTrait,
          typename DestroyTrait = LeakyInstanceTrait<T> >
struct LazyInstance {
  typedef typename LazyStaticInstance<T, CreateTrait, InitOnceTrait,
      DestroyTrait>::type type;
};


template <typename T,
          typename CreateTrait = DefaultCreateTrait<T>,
          typename InitOnceTrait = ThreadSafeInitOnceTrait,
          typename DestroyTrait = LeakyInstanceTrait<T> >
struct LazyDynamicInstance {
  typedef LazyInstanceImpl<T, DynamicallyAllocatedInstanceTrait<T>,
      CreateTrait, InitOnceTrait, DestroyTrait> type;
};

} // namespace base
#endif // BASE_LAZY_INSTANCE_H_
