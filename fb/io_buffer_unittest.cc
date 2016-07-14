#include "fb/io_buffer.h"
#include <gtest/gtest.h>

namespace fb {

TEST(IOBuffer, Simple) {
  std::unique_ptr<IOBuffer> buf(IOBuffer::Create(100));  
  uint32_t cap = buf->capacity();
  EXPECT_LE(100, cap);
}

} // namespace fb
