#ifndef FB_TEST_UTIL_H_
#define FB_TEST_UTIL_H_
#include "fb/test/time_util.h"
#include <gtest/gtest.h>

#define T_CHECK_TIMEOUT(start, end, expectedMS, ...) \
  EXPECT_TRUE(::fb::checkTimeout((start), (end),  \
                                 (expectedMS), false,  \
                                 ##__VA_ARGS__))

#define T_CHECK_TIME_LT(start, end, expectedMS, ...) \
  EXPECT_TRUE(::fb::checkTimeout((start), (end),  \
                                 (expectedMS), true, \
                                 ##__VA_ARGS__))


#endif // FB_TEST_UTIL_H_
