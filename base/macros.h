#ifndef BASE_MACROS_H_
#define BASE_MACROS_H_

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


#endif // BASE_MACROS_H_
