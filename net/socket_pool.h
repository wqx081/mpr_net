#ifndef WEBRTC_BASE_SOCKETPOOL_H_
#define WEBRTC_BASE_SOCKETPOOL_H_

#include <deque>
#include <list>

#include "net/logging.h"
#include "net/sigslot.h"
#include "net/socket_address.h"

namespace net {

class AsyncSocket;
class LoggingAdapter;
class SocketFactory;
class SocketStream;
class StreamInterface;

//////////////////////////////////////////////////////////////////////
// StreamPool
//////////////////////////////////////////////////////////////////////

class StreamPool {
public:
  virtual ~StreamPool() { }

  virtual StreamInterface* RequestConnectedStream(const SocketAddress& remote,
                                                  int* err) = 0;
  virtual void ReturnConnectedStream(StreamInterface* stream) = 0;
};

///////////////////////////////////////////////////////////////////////////////
// StreamCache - Caches a set of open streams, defers creation/destruction to
//  the supplied StreamPool.
///////////////////////////////////////////////////////////////////////////////

class StreamCache : public StreamPool, public sigslot::has_slots<> {
public:
  StreamCache(StreamPool* pool);
  ~StreamCache() override;

  // StreamPool Interface
  StreamInterface* RequestConnectedStream(const SocketAddress& remote,
                                          int* err) override;
  void ReturnConnectedStream(StreamInterface* stream) override;

private:
  typedef std::pair<SocketAddress, StreamInterface*> ConnectedStream;
  typedef std::list<ConnectedStream> ConnectedList;

  void OnStreamEvent(StreamInterface* stream, int events, int err);

  // We delegate stream creation and deletion to this pool.
  StreamPool* pool_;
  // Streams that are in use (returned from RequestConnectedStream).
  ConnectedList active_;
  // Streams which were returned to us, but are still open.
  ConnectedList cached_;
};

///////////////////////////////////////////////////////////////////////////////
// NewSocketPool
// Creates a new stream on every request
///////////////////////////////////////////////////////////////////////////////

class NewSocketPool : public StreamPool {
public:
  NewSocketPool(SocketFactory* factory);
  ~NewSocketPool() override;
  
  // StreamPool Interface
  StreamInterface* RequestConnectedStream(const SocketAddress& remote,
                                          int* err) override;
  void ReturnConnectedStream(StreamInterface* stream) override;

private:
  SocketFactory* factory_;
};

///////////////////////////////////////////////////////////////////////////////
// ReuseSocketPool
// Maintains a single socket at a time, and will reuse it without closing if
// the destination address is the same.
///////////////////////////////////////////////////////////////////////////////

class ReuseSocketPool : public StreamPool, public sigslot::has_slots<> {
public:
  ReuseSocketPool(SocketFactory* factory);
  ~ReuseSocketPool() override;

  // StreamPool Interface
  StreamInterface* RequestConnectedStream(const SocketAddress& remote,
                                          int* err) override;
  void ReturnConnectedStream(StreamInterface* stream) override;

private:
  void OnStreamEvent(StreamInterface* stream, int events, int err);

  SocketFactory* factory_;
  SocketStream* stream_;
  SocketAddress remote_;
  bool checked_out_;  // Whether the stream is currently checked out
};

///////////////////////////////////////////////////////////////////////////////
// LoggingPoolAdapter - Adapts a StreamPool to supply streams with attached
// LoggingAdapters.
///////////////////////////////////////////////////////////////////////////////

class LoggingPoolAdapter : public StreamPool {
public:
  LoggingPoolAdapter(StreamPool* pool, LoggingSeverity level,
                     const std::string& label, bool binary_mode);
  ~LoggingPoolAdapter() override;

  // StreamPool Interface
  StreamInterface* RequestConnectedStream(const SocketAddress& remote,
                                          int* err) override;
  void ReturnConnectedStream(StreamInterface* stream) override;

private:
  StreamPool* pool_;
  LoggingSeverity level_;
  std::string label_;
  bool binary_mode_;
  typedef std::deque<LoggingAdapter*> StreamList;
  StreamList recycle_bin_;
};

//////////////////////////////////////////////////////////////////////

}  // namespace rtc

#endif  // WEBRTC_BASE_SOCKETPOOL_H_
