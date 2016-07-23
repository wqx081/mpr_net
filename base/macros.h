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

inline uint64_t twang_mix64(uint64_t key) {
  key = (~key) + (key << 21);  // key *= (1 << 21) - 1; key -= 1;
  key = key ^ (key >> 24);
  key = key + (key << 3) + (key << 8);  // key *= 1 + (1 << 3) + (1 << 8)
  key = key ^ (key >> 14);
  key = key + (key << 2) + (key << 4);  // key *= 1 + (1 << 2) + (1 << 4)
  key = key ^ (key >> 28);
  key = key + (key << 31);  // key *= 1 + (1 << 31)
  return key;
}


#if 0
#define FOLLY_HAS_TRUE_XXX(name)                          \
  BOOST_MPL_HAS_XXX_TRAIT_DEF(name);                      \
  template <class T> struct name ## _is_true              \
    : std::is_same<typename T::name, std::true_type> {};  \
  template <class T> struct has_true_ ## name             \
  : std::conditional<                                   \
          has_ ## name <T>::value,                          \
          name ## _is_true<T>,                              \
          std::false_type                                   \
        >:: type {};
  
FOLLY_HAS_TRUE_XXX(IsRelocatable)
FOLLY_HAS_TRUE_XXX(IsZeroInitializable)
FOLLY_HAS_TRUE_XXX(IsTriviallyCopyable)
	  
#undef FOLLY_HAS_TRUE_XXX
#endif

#define AVOID_FALSE_SHARING __attribute__((__aligned__(128)))

enum {
  kFalseSharingRange = 128
};



#include <boost/type_traits.hpp>
#include <boost/mpl/and.hpp>
#include <boost/mpl/has_xxx.hpp>
#include <boost/mpl/not.hpp>

namespace traits_detail {
	  
#define FOLLY_HAS_TRUE_XXX(name)                          \
  BOOST_MPL_HAS_XXX_TRAIT_DEF(name);                      \
  template <class T> struct name ## _is_true              \
    : std::is_same<typename T::name, std::true_type> {};  \
  template <class T> struct has_true_ ## name             \
    : std::conditional<                                   \
        has_ ## name <T>::value,                          \
        name ## _is_true<T>,                              \
        std::false_type                                   \
>:: type {};
	  
FOLLY_HAS_TRUE_XXX(IsRelocatable)
FOLLY_HAS_TRUE_XXX(IsZeroInitializable)
FOLLY_HAS_TRUE_XXX(IsTriviallyCopyable)
		        
#undef FOLLY_HAS_TRUE_XXX
}

template <class T> struct IsTriviallyCopyable
    : std::integral_constant<bool,
	        !std::is_class<T>::value ||
	             traits_detail::has_true_IsTriviallyCopyable<T>::value
	           > {};
  
template <class T> struct IsRelocatable
      : std::integral_constant<bool,
	        !std::is_class<T>::value ||
	               IsTriviallyCopyable<T>::value ||
	               traits_detail::has_true_IsRelocatable<T>::value
	             > {};
      
template <class T> struct IsZeroInitializable
      : std::integral_constant<bool,
	        !std::is_class<T>::value ||
	               traits_detail::has_true_IsZeroInitializable<T>::value
	             > {};


#include <utility>
#include <functional>
#include <tuple>

namespace fb {

inline uint64_t hash_128_to_64(const uint64_t upper, const uint64_t lower) {
	  const uint64_t kMul = 0x9ddfea08eb382d69ULL;
	    uint64_t a = (lower ^ upper) * kMul;
	      a ^= (a >> 47);
	        uint64_t b = (upper ^ a) * kMul;
		  b ^= (b >> 47);
		    b *= kMul;
		      return b;
}


template <class Hasher>
inline size_t hash_combine_generic() {
  return 0;
}

template <
    class Iter,
     class Hash = std::hash<typename std::iterator_traits<Iter>::value_type>>
     uint64_t hash_range(Iter begin,
Iter end,
uint64_t hash = 0,
Hash hasher = Hash()) {
  for (; begin != end; ++begin) {
  hash = hash_128_to_64(hash, hasher(*begin));
}
return hash;
}

template <class Hasher, typename T, typename... Ts>
	size_t hash_combine_generic(const T& t, const Ts&... ts) {
		  size_t seed = Hasher::hash(t);
		    if (sizeof...(ts) == 0) {
			        return seed;
				  }
		      size_t remainder = hash_combine_generic<Hasher>(ts...);
		        return hash_128_to_64(seed, remainder);
	}


class StdHasher {
 public:
  template <typename T>
  static size_t hash(const T& t) {
    return std::hash<T>()(t);
  }
};

template <typename T, typename... Ts>
size_t hash_combine(const T& t, const Ts&... ts) {
  return hash_combine_generic<StdHasher>(t, ts...);
}

} // namespace fb
namespace std {
template <typename T1, typename T2>
struct hash<std::pair<T1, T2> > {
 public:
  size_t operator()(const std::pair<T1, T2>& x) const {
    return fb::hash_combine(x.first, x.second);
  }
};
} // namespace std

#endif // BASE_MACROS_H_
