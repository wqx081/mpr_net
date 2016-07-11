#include "net/net_helpers.h"
#include "net/async_socket.h"
#include "net/physical_socket_server.h"
#include "net/thread.h"

#include <gtest/gtest.h>

using namespace base;
using namespace net;

TEST(Server, Basic) {
  SocketAddress loopback;

  Thread* main = Thread::Current();
  AsyncSocket* socket = main->socketserver()
	  ->CreateAsyncSocket(loopback.family(), SOCK_DGRAM);
  socket->Bind(loopback);
   
  EXPECT_TRUE(true);
}
