#ifndef BASE_MACROS_H_
#define BASE_MACROS_H_

#include <stdio.h>
#include <assert.h>
#include <inttypes.h>
#include <stdint.h>

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

#endif // BASE_MACROS_H_
