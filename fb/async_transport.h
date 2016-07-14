#ifndef FB_ASYNC_TRANSPORT_H_
#define FB_ASYNC_TRANSPORT_H_
#include <memory>
#include <sys/uio.h>

#include "fb/delayed_destruction.h"
#include "fb/event_base.h"
#include "fb/async_socket_base.h"

namespace fb {

class EventBase;
class IOBuffer;
class SocketAddress;


enum class WriteFlags : uint32_t {
  NONE = 0x00,
  CORK = 0x01,
  EOR = 0x02,
};

class AsyncTransport : public DelayedDestruction,
                       public AsyncSocketBase {
 public:
  typedef std::unique_ptr<AsyncTransport, Destructor> UniquePtr;
  
  virtual void Close() = 0;
  virtual void CloseNow() = 0;
  virtual void CloseWithReset();
  virtual void ShutdownWrite() =0;
  virtual void ShutdownWriteNow() = 0;
  virtual bool Good() const = 0;
  virtual bool Readable() const = 0;
  virtual bool IsPending() const;
  virtual bool Connecting() const = 0;
  virtual bool Error() const = 0;
  virtual void AttachEventBase(EventBase* event_base) = 0;
  virtual void DetachEventBase() = 0;
  virtual bool IsDetachable() const = 0;
  virtual void SetSendTimeout(uint32_t milliseconds) = 0;
  virtual uint32_t GetSendTimeout() const = 0;
  virtual void GetLocalAddress(SocketAddress* address) const = 0;
  virtual void GetAddress(SocketAddress* address) const;
  virtual void GetPeerAddress(SocketAddress* address) const = 0;
  virtual bool IsEorTrackingEnabled() const = 0;
  virtual void SetEorTracking(bool track) = 0;
  
  virtual size_t GetAppBytesWritten() const = 0;
  virtual size_t GetRawBytesWritten() const = 0;
  virtual size_t GetAppBytesReceived() const = 0;
  virtual size_t GetRawBytesReceived() const = 0;
 protected:
  virtual ~AsyncTransport() {}
};

class AsyncTransportWrapper : virtual public AsyncTransport {
 public:
  typedef std::unique_ptr<AsyncTransportWrapper, Destructor> UniquePtr;
  
  class ReadCallback {
   public:
    virtual ~ReadCallback() {}
    virtual void GetReadBuffer(void* buf_return, size_t* len_return) = 0;
    virtual void ReadDataAvailable(size_t len) noexcept = 0;
    virtual void ReadEOF() noexcept = 0;
    //TODO(wqx):
    //virtual void ReadError(const AsyncSocketException& ex) noexcept = 0;
  };
  class WriteCallback {
   public:
    virtual ~WriteCallback() {}
    virtual void WriteSuccess() noexcept = 0;
    //TODO(wqx):
    //virtual void WriteError(size_t bytes_written, const AsyncSocketException& e) noexcept = 0;
  };

  virtual void SetReadCB(ReadCallback* cb) = 0;
  virtual ReadCallback* GetReadCB() const = 0;
  virtual void Write(WriteCallback* cb,
                     const void* buf,
                     size_t bytes,
                     WriteFlags flags = WriteFlags::NONE) = 0;
  virtual void Writev(WriteCallback* cb,
                      const iovec* vec,
                      size_t count,
                      WriteFlags flags = WriteFlags::NONE) = 0;
  //TODO(wqx):
  //
  //virtual void WriteChain(WriteCallback* cb,
  //                        std::unique_ptr<IOBuffer>&& buf,
  //                        WriteFlags flags = WriteFlags::NONE) = 0;
};

} // namespace fb
#endif // FB_ASYNC_TRANSPORT_H_
