#include "base/macros.h"

#include "net/null_socket_server.h"

namespace net {

NullSocketServer::NullSocketServer() : event_(false, false) {}
NullSocketServer::~NullSocketServer() {}

bool NullSocketServer::Wait(int cms, bool /* process_io */) {
  event_.Wait(cms);
  return true;
}

void NullSocketServer::WakeUp() {
  event_.Set();
}

Socket* NullSocketServer::CreateSocket(int /* type */) {
  //RTC_NOTREACHED();
  return nullptr;
}

Socket* NullSocketServer::CreateSocket(int /* family */, int /* type */) {
  //RTC_NOTREACHED();
  return nullptr;
}

AsyncSocket* NullSocketServer::CreateAsyncSocket(int /* type */) {
  //RTC_NOTREACHED();
  return nullptr;
}

AsyncSocket* NullSocketServer::CreateAsyncSocket(int /* family */,
                                                      int /* type */) {
  //RTC_NOTREACHED();
  return nullptr;
}


}  // namespace rtc
