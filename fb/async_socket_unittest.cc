#include <iostream>
#include "fb/async_socket.h"
#include "fb/async_server_socket.h"
#include "fb/event_base.h"

#include <sys/poll.h>
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
  ConnectCallback(): state(STATE_WAITING), exception(AsyncSocketException::UNKNOWN, "none") {}

  void ConnectSuccess() noexcept override {
    state = STATE_SUCCEEDED;  
    if (success_callback) {
      success_callback();
    }
  }
  void ConnectError(const AsyncSocketException& ex) noexcept override {
    state = STATE_FAILED;
    exception = ex;
    if (error_callback) {
      error_callback();
    }
  }

  StateEnum state;
  AsyncSocketException exception;
  VoidCallback success_callback;
  VoidCallback error_callback;
};

class WriteCallback : public AsyncTransportWrapper::WriteCallback {
 public:
  WriteCallback(): state(STATE_WAITING), bytes_written(0), exception(AsyncSocketException::UNKNOWN, "none") {}

  void WriteSuccess() noexcept override {
    state = STATE_SUCCEEDED;
    if (success_callback) {
      success_callback();
    }
  }
  void WriteError(size_t bytes_written,
                  const AsyncSocketException& ex) noexcept override {
    state = STATE_FAILED;
    this->bytes_written = bytes_written;
    exception = ex;
    if (error_callback) {
      error_callback();
    }
  }

  StateEnum state;
  size_t bytes_written;
  AsyncSocketException exception;
  VoidCallback success_callback;
  VoidCallback error_callback;
};

class ReadCallback : public AsyncTransportWrapper::ReadCallback {
 public:
  ReadCallback() : state(STATE_WAITING),
      exception(AsyncSocketException::UNKNOWN, "none"),
      buffers() {}

  ~ReadCallback() {
    for (std::vector<Buffer>::iterator it = buffers.begin();
         it != buffers.end();
         ++it) {
      it->Free();
    }
    current_buffer.Free();
  }

  void GetReadBuffer(void** buf_return, size_t* len_return) override {
    if (!current_buffer.buffer) {
      current_buffer.Allocate(4096);
    }
    *buf_return = current_buffer.buffer;
    *len_return = current_buffer.length;
  }
  void ReadDataAvailable(size_t len) noexcept override {
    current_buffer.length = len;
    buffers.push_back(current_buffer);
    current_buffer.Reset();
    if (data_available_callback) {
      data_available_callback();
    }
  }
  void ReadEOF() noexcept override {
    state = STATE_SUCCEEDED;
  }
  void ReadError(const AsyncSocketException& ex) noexcept override {
    state = STATE_FAILED;
    exception = ex;
  }

  void VerifyData(const char* expected, size_t expected_len) const {
    size_t offset = 0;
    for (size_t idx = 0; idx < buffers.size(); ++idx) {
      const auto& buf = buffers[idx];
      size_t cmp_len = std::min(buf.length, expected_len - offset);
      CHECK_EQ(memcmp(buf.buffer, expected + offset, cmp_len), 0);
      CHECK_EQ(cmp_len, buf.length);
      offset += cmp_len;
    }
    CHECK_EQ(offset, expected_len);
  }

  class Buffer {
   public:
    Buffer() : buffer(nullptr), length(0) {}
    Buffer(char* buf, size_t len) : buffer(buf), length(len) {}

    void Reset() {
      buffer = nullptr;
      length = 0;
    }
    void Allocate(size_t length) {
      assert(buffer == nullptr);
      this->buffer = static_cast<char *>(malloc(length));
      this->length = length;
    }
    void Free() {
      ::free(buffer);
      Reset();
    }

    char* buffer;
    size_t length;
  };

  StateEnum state;
  AsyncSocketException exception;
  std::vector<Buffer> buffers;
  Buffer current_buffer;
  VoidCallback data_available_callback;  
};

class BlockingSocket : public AsyncSocket::ConnectCallback,
                       public AsyncTransportWrapper::ReadCallback,
                       public AsyncTransportWrapper::WriteCallback {
 public:
  explicit BlockingSocket(int fd)
    : sock_(new AsyncSocket(&event_base_, fd)) {}

  void Open() {
    sock_->Connect(this, address_);
    event_base_.Loop();
  }
  void Close() {
    sock_->Close();
  }
  int32_t Write(const uint8_t* buf, size_t len) {
    sock_->Write(this, buf, len);
    event_base_.Loop();
    return len;
  }
  void Flush() {}
  int32_t ReadAll(uint8_t* buf, size_t len) {
    return ReadHelper(buf, len, true);
  }
  int32_t Read(uint8_t* buf, size_t len) {
    return ReadHelper(buf, len, false);
  }
  int GetSocketFD() const {
    return sock_->GetFd();
  }

 private:
  EventBase event_base_;
  AsyncSocket::UniquePtr sock_;
  uint8_t* read_buf_{nullptr};  
  size_t read_len_{0};
  net::SocketAddress address_;

  void ConnectSuccess() noexcept override {}
  void ConnectError(const AsyncSocketException& ex) noexcept override {
    LOG(ERROR) << "ConnectError: " << ex.what();
  }
  void GetReadBuffer(void** buf_return, size_t* len_return) override {
    *buf_return = read_buf_;
    *len_return = read_len_;
  }
  void ReadDataAvailable(size_t len) noexcept override {
    read_buf_ += len;
    read_len_ -= len;
    if (read_len_ == 0) {
      sock_->SetReadCB(nullptr);
    }
  }
  void ReadEOF() noexcept override {}
  void ReadError(const AsyncSocketException& ex) noexcept override {
    LOG(ERROR) << "ReadError: " << ex.what();
  }

  void WriteSuccess() noexcept override {}
  void WriteError(size_t bytes_written,
                  const AsyncSocketException& ex) noexcept override {
    (void) bytes_written;
    LOG(ERROR) << "WriteError: " << ex.what();
  }

  int32_t ReadHelper(uint8_t* buf, size_t len, bool all) {
    read_buf_ = buf;
    read_len_ = len;
    sock_->SetReadCB(this);
    while (sock_->Good() && read_len_ > 0) {
      event_base_.Loop();
      if (!all) {
        break;
      }
    }
    sock_->SetReadCB(nullptr);
    if (all && read_len_ > 0) {
      throw AsyncSocketException(AsyncSocketException::UNKNOWN,
          "eof");
    }
    return len - read_len_;
  }
};

class TestServer {
 public:
  TestServer() : fd_(-1) {
    fd_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ < 0) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
          "failed to create test server socket", errno);
    }
    if (fcntl(fd_, F_SETFL, O_NONBLOCK) != 0) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
          "failed to put test server socket in non-blocking mode", errno);
    }
    if (listen(fd_, 10) != 0) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
          "failed to listen on test server socket",
          errno);
    }
    address_.SetFromLocalAddress(fd_);
    address_.SetIP("127.0.0.1");
  }
  const net::SocketAddress& GetAddress() const {
    return address_;
  }
  int AcceptFd(int timeout=50) {
    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLIN;
    int ret = poll(&pfd, 1, timeout);
    if (ret == 0) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
          "test server accept() timeout");
    } else if (ret < 0) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
          "test server accept() poll failed", errno);
    }

    int accepted_fd = ::accept(fd_, nullptr, nullptr);
    if (accepted_fd < 0) {
      throw AsyncSocketException(AsyncSocketException::INTERNAL_ERROR,
          "test server accept() failed", errno);
    }

    return accepted_fd;
  }

  std::shared_ptr<BlockingSocket> Accept(int timeout=50) {
    int fd = AcceptFd(timeout);
    return std::shared_ptr<BlockingSocket>(new BlockingSocket(fd));
  }
  std::shared_ptr<AsyncSocket> AcceptAsync(EventBase* event_base, int timeout=50) {
    int fd = AcceptFd(timeout);
    return AsyncSocket::NewSocket(event_base, fd);
  }

  void VerifyConnection(const char* buf, size_t len) {
    std::shared_ptr<BlockingSocket> accepted_socket = Accept();
    std::unique_ptr<uint8_t> read_buf(new uint8_t[len]);
    accepted_socket->ReadAll(read_buf.get(), len);
    CHECK_EQ(memcmp(buf, read_buf.get(), len), 0);
    uint32_t bytes_read = accepted_socket->Read(read_buf.get(), len);
    CHECK_EQ(bytes_read, 0);
  }

 private:
  int fd_;
  net::SocketAddress address_;
};


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


// Test connecting to a server
TEST(AsyncSocketTest, Connect) {
  TestServer server;

  EventBase evb;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&evb);
  ConnectCallback cb;
  socket->Connect(&cb, server.GetAddress(), 30);

  evb.Loop();

  CHECK_EQ(cb.state, STATE_SUCCEEDED);
}

// Test connecting to a server that isn't listening
TEST(AsyncSocketTest, ConnectRefused) {
  EventBase evb;

  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&evb);

  net::SocketAddress addr("127.0.0.1", 65535);
  ConnectCallback cb;
  socket->Connect(&cb, addr, 30);

  evb.Loop();

  CHECK_EQ(cb.state, STATE_FAILED);
  CHECK_EQ(cb.exception.GetType(), AsyncSocketException::NOT_OPEN);
}

// Test connection timeout
TEST(AsyncSocketTest, ConnectTimeout) {
  EventBase event_base;

  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base);

  net::SocketAddress addr("8.8.8.8", 65535);
  ConnectCallback cb;
  socket->Connect(&cb, addr, 1);
  
  event_base.Loop();

  CHECK_EQ(cb.state, STATE_FAILED);
  CHECK_EQ(cb.exception.GetType(), AsyncSocketException::TIMED_OUT);

  net::SocketAddress peer;
  socket->GetPeerAddress(&peer);
  CHECK_EQ(peer, addr);
}

// Test writing immediately after connecting, without waiting for connect to finish
TEST(AsyncSocketTest, ConnectAndWrite) {
  TestServer server;

  // connect
  EventBase event_base;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base);
  ConnectCallback ccb;
  socket->Connect(&ccb, server.GetAddress(), 30);

  // Write
  char buf[128];
  memset(buf, 'a', sizeof(buf));
  WriteCallback wcb;
  socket->Write(&wcb, buf, sizeof(buf));

  event_base.Loop();

  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb.state, STATE_SUCCEEDED);

  socket->Close();
  server.VerifyConnection(buf, sizeof(buf));
}

// Test connecting using a nullptr connect callback
TEST(AsyncSocketTest, ConnectNullCallback) {
  TestServer server;

  // connect
  EventBase event_base;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base);
  socket->Connect(nullptr, server.GetAddress(), 30);

  char buf[128];
  memset(buf, 'a', sizeof(buf));
  WriteCallback wcb;
  socket->Write(&wcb, buf, sizeof(buf));

  event_base.Loop();

  CHECK_EQ(wcb.state, STATE_SUCCEEDED);

  socket->Close();
  server.VerifyConnection(buf, sizeof(buf));
}

/**
 * Test calling both write() and close() immediately after connecting, without
 * waiting for connect to finish.
 *
 * This exercises the STATE_CONNECTING_CLOSING code.
 */
TEST(AsyncSocketTest, ConnectWriteAndClose) {

  TestServer server;

  EventBase event_base;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base);
  ConnectCallback ccb;
  socket->Connect(&ccb, server.GetAddress(), 30);

  char buf[128];
  memset(buf, 'a', sizeof(buf));
  WriteCallback wcb;
  socket->Write(&wcb, buf, sizeof(buf));

  // close
  socket->Close();

  event_base.Loop();

  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(wcb.state, STATE_SUCCEEDED);

  server.VerifyConnection(buf, sizeof(buf));
}

// Test calling close() immediately after connect()
TEST(AsyncSocketTest, ConnectAndClose) {
  TestServer server;

  EventBase event_base;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base);
  ConnectCallback ccb;
  socket->Connect(&ccb, server.GetAddress(), 30);

  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; aborting test "
                 "of close-during-connect behavior";
    return;
  }

  socket->Close();

  event_base.Loop();

  CHECK_EQ(ccb.state, STATE_FAILED);
}

// Test calling CloseNow() immediately after connect()
// This should be identical to the normal close behavior.
TEST(AsyncSocketTest, ConnectAndCloseNow) {
  TestServer server;

  EventBase event_base;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base);
  ConnectCallback ccb;
  socket->Connect(&ccb, server.GetAddress(), 30);

  if (ccb.state == STATE_SUCCEEDED) {
    LOG(INFO) << "connect() succeeded immediately; aborting test "
                 "of close-during-connect behavior";
    return;
  }

  socket->CloseNow();

  event_base.Loop();

  CHECK_EQ(ccb.state, STATE_FAILED);
}

// Test calling both Write() and CloseNow() immediately after connecting,
// without waiting for connect to finish.
//
// This should abort the pending write.
TEST(AsyncSocketTest, ConnectWriteAndCloseNow) {

  TestServer server;

  EventBase event_base;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base);
  ConnectCallback ccb;
  socket->Connect(&ccb, server.GetAddress(), 30);

  char buf[128];
  memset(buf, 'a', sizeof(buf));
  WriteCallback wcb;
  socket->Write(&wcb, buf, sizeof(buf));

  socket->CloseNow();

  event_base.Loop();

  CHECK_EQ(ccb.state, STATE_FAILED);
  CHECK_EQ(wcb.state, STATE_FAILED);
}

// Test installing a read callback immediately, before connect() finishes.
TEST(AsyncSocketTest, ConnectAndRead) {

  TestServer server;

  EventBase event_base;
  std::shared_ptr<AsyncSocket> socket = AsyncSocket::NewSocket(&event_base);
  ConnectCallback ccb;
  socket->Connect(&ccb, server.GetAddress(), 30);

  ReadCallback rcb;
  socket->SetReadCB(&rcb);

  std::shared_ptr<BlockingSocket> accepted_socket = server.Accept();
  uint8_t buf[128];
  memset(buf, 'a', sizeof(buf));
  accepted_socket->Write(buf, sizeof(buf));
  accepted_socket->Flush();
  accepted_socket->Close();

  event_base.Loop();

  CHECK_EQ(ccb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.state, STATE_SUCCEEDED);
  CHECK_EQ(rcb.buffers.size(), 1);
  CHECK_EQ(rcb.buffers[0].length, sizeof(buf));
  CHECK_EQ(memcmp(rcb.buffers[0].buffer, buf, sizeof(buf)), 0);
}

// Test installing a read callback and then closing immediately before the
// connect attempt finishes.
TEST(AsyncSocketTest, ConnectReadAndClose) {

  TestServer server;

  EventBase event_base;
  std::shared_ptr<AsyncSocket> connect_socket = AsyncSocket::NewSocket(&event_base);
  ConnectCallback ccb;
  connect_socket->Connect(&ccb, server.GetAddress(), 30);

  ReadCallback rcb;
  connect_socket->SetReadCB(&rcb);

  connect_socket->Close();

  event_base.Loop();

  CHECK_EQ(ccb.state, STATE_FAILED);
  CHECK_EQ(rcb.buffers.size(), 0);
  CHECK_EQ(rcb.state, STATE_SUCCEEDED);
}

// Test both writing and installing a read callback immediately,
// before connect() finishes.
//TODO
