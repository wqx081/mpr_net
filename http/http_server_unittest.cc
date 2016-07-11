#include "net/socket_address.h"
#include "net/physical_socket_server.h"
#include "net/sigslot.h"
#include "http/http_server.h"
#include <gtest/gtest.h>

namespace net {

TEST(HttpServer, Basic) {
  SocketAddress listen_addr("127.0.0.1", 9999);   
  std::unique_ptr<HttpListenServer> http_server(new HttpListenServer());
  std::unique_ptr<SocketServer> ss(new PhysicalSocketServer());

  EXPECT_TRUE(http_server->Listen(listen_addr) == 0);
  Thread* main = Thread::Current();
  main->Run();

  EXPECT_TRUE(true);
}

} // namespace net
