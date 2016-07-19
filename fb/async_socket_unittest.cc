#include <iostream>
#include "fb/async_socket.h"
#include "fb/async_server_socket.h"
#include "fb/event_base.h"

#include <gtest/gtest.h>

namespace fb {

TEST(AsyncSocket, GetSockOpt) {
  EventBase event_base;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base, 0);

  int val;
  socklen_t len;
  int exected_rc = getsockopt(socket->GetFd(),
                              SOL_SOCKET,
                              SO_REUSEADDR,
                              &val,
                              &len);
  int actual_rc = socket->GetSockOpt(SOL_SOCKET, SO_REUSEADDR, &val, &len);
  EXPECT_EQ(exected_rc, actual_rc);
}

} // namespace fb
