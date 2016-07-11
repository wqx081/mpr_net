#ifndef WEBRTC_BASE_SOCKETSTREAM_H_
#define WEBRTC_BASE_SOCKETSTREAM_H_

#if 0
#include "webrtc/base/asyncsocket.h"
#include "webrtc/base/common.h"
#include "webrtc/base/constructormagic.h"
#include "webrtc/base/stream.h"
#endif
#include "net/async_socket.h"
#include "net/stream.h"


namespace net {

///////////////////////////////////////////////////////////////////////////////

class SocketStream : public StreamInterface, public sigslot::has_slots<> {
 public:
  explicit SocketStream(AsyncSocket* socket);
  ~SocketStream() override;

  void Attach(AsyncSocket* socket);
  AsyncSocket* Detach();

  AsyncSocket* GetSocket() { return socket_; }

  StreamState GetState() const override;

  StreamResult Read(void* buffer,
                    size_t buffer_len,
                    size_t* read,
                    int* error) override;

  StreamResult Write(const void* data,
                     size_t data_len,
                     size_t* written,
                     int* error) override;

  void Close() override;

 private:
  void OnConnectEvent(AsyncSocket* socket);
  void OnReadEvent(AsyncSocket* socket);
  void OnWriteEvent(AsyncSocket* socket);
  void OnCloseEvent(AsyncSocket* socket, int err);

  AsyncSocket* socket_;

  DISALLOW_COPY_AND_ASSIGN(SocketStream);
};

///////////////////////////////////////////////////////////////////////////////

}  // namespace rtc

#endif  // WEBRTC_BASE_SOCKETSTREAM_H_
