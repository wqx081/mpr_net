#include "fb/test/socket_pair.h"


#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdexcept>

#include <glog/logging.h>

namespace fb {

SocketPair::SocketPair(Mode mode) {
  if (socketpair(PF_UNIX, SOCK_STREAM, 0, fds_) != 0) {
    LOG(ERROR) <<"test::SocketPair: failed create socket pair"; 
  }

  if (mode == NONBLOCKING) {
    if (fcntl(fds_[0], F_SETFL, O_NONBLOCK) != 0) {
      LOG(ERROR) << 
        "test::SocketPair: failed to set non-blocking "
        "read mode";
    }
    if (fcntl(fds_[1], F_SETFL, O_NONBLOCK) != 0) {
      LOG(ERROR) << 
        "test::SocketPair: failed to set non-blocking "
        "write mode";
    }
  }
}

SocketPair::~SocketPair() {
  closeFD0();
  closeFD1();
}

void SocketPair::closeFD0() {
  if (fds_[0] >= 0) {
    close(fds_[0]);
    fds_[0] = -1;
  }
}

void SocketPair::closeFD1() {
  if (fds_[1] >= 0) {
    close(fds_[1]);
    fds_[1] = -1;
  }
}

}
