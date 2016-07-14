#pragma once

namespace fb {

class SocketPair {
 public:
  enum Mode {
    BLOCKING,
    NONBLOCKING
  };

  explicit SocketPair(Mode mode = NONBLOCKING);
  ~SocketPair();

  int operator[](int index) const {
    return fds_[index];
  }

  void closeFD0();
  void closeFD1();

  int extractFD0() {
    return extractFD(0);
  }
  int extractFD1() {
    return extractFD(1);
  }
  int extractFD(int index) {
    int fd = fds_[index];
    fds_[index] = -1;
    return fd;
  }

 private:
  int fds_[2];
};

}
