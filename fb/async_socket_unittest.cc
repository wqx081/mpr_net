#include <iostream>
#include "fb/async_socket.h"
#include "fb/async_server_socket.h"
#include "fb/event_base.h"

#include <gtest/gtest.h>

using namespace fb;

typedef std::function<void()> VoidCallback;

enum StateEnum {
  STATE_WAITING,
  STATE_SUCCEEDED,
  STATE_FAILED,
};

class ConnectCallback : public AsyncSocket::ConnectCallback {
 public:
  ConnectCallback(): state(STATE_WAITING) {}
  void ConnectSuccess() noexcept override {
    state = STATE_SUCCEEDED;  
  }
  void ConnectError(const AsyncSocketException& ) noexcept override {
    state = STATE_FAILED;
  }

  StateEnum state;
};

TEST(AsyncSocket, Connect) {

  EventBase event_base;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base);
  ConnectCallback cb;
  net::SocketAddress address("0.0.0.0", 80);
  socket->Connect(&cb, address, 30);

  event_base.Loop();

  EXPECT_EQ(cb.state, STATE_SUCCEEDED);
}

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


