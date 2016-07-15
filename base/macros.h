#ifndef BASE_MACROS_H_
#define BASE_MACROS_H_

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <type_traits>
#include <memory>
#include <glog/logging.h>

#define OFFSET_OF(type, field) \
  (reinterpret_cast<intptr_t>(&(reinterpret_cast<type*>(16)->field)) - 16)

template<typename T, size_t N>
char (&ArraySizeHelper(T (&array)[N]))[N];
#define ARRAYSIZE(array) (sizeof(ArraySizeHelper(array)))

template<typename Dest, typename Source>
Dest bit_cast(const Source& source) {
  static_assert(sizeof(Dest) == sizeof(Source), 
                "source and dest must be same size");
  Dest dest;
  memcpy(&dest, &source, sizeof(dest));
  return dest;
}

#define DISALLOW_ASSIGN(TypeName) \
  void operator=(const TypeName&)
#define DISALLOW_COPY_AND_ASSIGN(TypeName) \
  TypeName(const TypeName&) = delete;      \
  void operator=(const TypeName&) = delete


template<typename T>
inline void USE(T) {}

#define STATIC_ASSERT(test) static_assert(test, #test)

// C++11
//#define MPR_ALIGNED(n) alignas(n)
#define MPR_ALIGNED(n) __attribute__((aligned(n)))
#define MPR_ALIGNAS(type, alignment) __attribute__((aligned(__alignof__(type))))
#define MPR_ALIGNOF(type) __alignof(type)

#define MPR_UNLIKELY(condition) (__builtin_expect(!!(condition), 0))
#define MPR_LIKELY(condition) (__builtin_expect(!!(condition), 1))

#define GetLastError() errno
#define LAST_SYSTEM_ERROR (GetLastError())

#define STACK_ARRAY(TYPE, LEN) static_cast<TYPE*>(::alloca((LEN)*sizeof(TYPE)))

#define ALIGNP(p, t)                                             \
	  (reinterpret_cast<uint8_t*>(((reinterpret_cast<uintptr_t>(p) + \
					  ((t) - 1)) & ~((t) - 1))))

#define FALLTHROUGH() do {} while (0)

// for fb
//
struct MaxAlign { char c; } __attribute__((__aligned__));

#define FB_PACK_ATTR  __attribute__((__packed__))/**/
#define FB_PACK_PUSH /**/
#define FB_PACK_POP /**/

inline size_t GoodMallocSize(size_t min_size) noexcept {
  return min_size;
}

template<typename T, typename Dp=std::default_delete<T>, typename... Args>
typename std::enable_if<!std::is_array<T>::value, std::unique_ptr<T, Dp>>::type
make_unique(Args&&... args) {
  return std::unique_ptr<T, Dp>(new T(std::forward<Args>(args)...));
}

template<typename T, typename Dp = std::default_delete<T>>
typename std::enable_if<std::is_array<T>::value, std::unique_ptr<T, Dp>>::type
make_unique(const size_t n) {
  return std::unique_ptr<T, Dp>(new typename std::remove_extent<T>::type[n]());
}

template<typename T, typename Dp = std::default_delete<T>, typename... Args>
typename std::enable_if<std::extent<T>::value != 0, std::unique_ptr<T, Dp>>::type
make_unique(Args&&...) = delete;



#define FOR_EACH(i, c)                              \
  if (bool FOR_EACH_state1 = false) {} else         \
    for (auto && FOR_EACH_state2 = (c);             \
         !FOR_EACH_state1; FOR_EACH_state1 = true)  \
      for (auto i = FOR_EACH_state2.begin();        \
           i != FOR_EACH_state2.end(); ++i)
  

  template <class T, class U>
  typename std::enable_if<
    (std::is_arithmetic<T>::value && std::is_arithmetic<U>::value) ||
    (std::is_pointer<T>::value && std::is_pointer<U>::value),
    bool>::type
  NotThereYet(T& iter, const U& end) {
    return iter < end;
  }
  
  template <class T, class U>
  typename std::enable_if<
    !(
      (std::is_arithmetic<T>::value && std::is_arithmetic<U>::value) ||
      (std::is_pointer<T>::value && std::is_pointer<U>::value)
    ),
    bool>::type
  NotThereYet(T& iter, const U& end) {
    return iter != end;
  }

#define FOR_EACH_RANGE(i, begin, end)                      \
    for (auto i = (true ? (begin) : (end));                \
                  NotThereYet(i, (end));                   \
                  ++i)

#endif // BASE_MACROS_H_
