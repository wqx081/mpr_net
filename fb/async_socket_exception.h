#ifndef FB_ASYNC_SOCKET_EXCEPTION_H_
#define FB_ASYNC_SOCKET_EXCEPTION_H_

#include "fb/delayed_destruction.h"
#include <stdexcept>
#include <string>

namespace fb {

class AsyncSocketException : public std::runtime_error {
 public:

  enum AsyncSocketExceptionType {   
      UNKNOWN = 0
    , NOT_OPEN = 1
    , ALREADY_OPEN = 2
    , TIMED_OUT = 3
    , END_OF_FILE = 4
    , INTERRUPTED = 5
    , BAD_ARGS = 6
    , CORRUPTED_DATA = 7
    , INTERNAL_ERROR = 8
    , NOT_SUPPORTED = 9
    , INVALID_STATE = 10
    , SSL_ERROR = 12
    , COULD_NOT_BIND = 13
    , SASL_HANDSHAKE_TIMEOUT = 14
  };

  AsyncSocketExceptionType type_;
  int errno_;

  AsyncSocketException(AsyncSocketExceptionType type, const std::string& message)
      : std::runtime_error(message),
        type_(type),
        errno_(0) {}
  AsyncSocketException(AsyncSocketExceptionType type,
                       const std::string& message,
                       int errno_copy) :
        std::runtime_error(GetMessage(message, errno_copy)),
        type_(type),
        errno_(errno_copy) {}

  AsyncSocketExceptionType GetType() const noexcept { return type_; }
  int GetErrno() const noexcept { return errno_; }

 protected:
  std::string strerror_s(int errno_copy) {
    return "errno = " + std::to_string(errno_copy);
  }  
  std::string GetMessage(const std::string& message,
                         int errno_copy) {
    if (errno_copy != 0) {
      return message + ": " + strerror_s(errno_copy);
    } else {
      return message;
    }
  }
};

} // namespace fb
#endif // FB_ASYNC_SOCKET_EXCEPTION_H_
