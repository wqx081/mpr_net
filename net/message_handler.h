#ifndef NET_MESSAGE_HANDLER_H_
#define NET_MESSAGE_HANDLER_H_
#include "base/macros.h"
#include <memory>
#include <utility>

namespace net {

struct Message;

// Message get dispatched to a MessageHandler
class MessageHandler {
 public:
  virtual ~MessageHandler();
  virtual void OnMessage(Message* msg) = 0;
  
 protected:
  MessageHandler() {}
  
 private:
  DISALLOW_COPY_AND_ASSIGN(MessageHandler);
};

template <class ReturnT, class FunctorT>
class FunctorMessageHandler : public MessageHandler {
 public:
  explicit FunctorMessageHandler(const FunctorT& functor)
        : functor_(functor) {}
  virtual void OnMessage(Message* msg) {
    (void)msg;
    result_ = functor_();
  }

  const ReturnT& result() const { return result_; }
  
 private:
  FunctorT functor_;
  ReturnT result_;
};

template <class ReturnT, class FunctorT>
class FunctorMessageHandler<class std::unique_ptr<ReturnT>, FunctorT>
      : public MessageHandler {
 public:
  explicit FunctorMessageHandler(const FunctorT& functor) : functor_(functor) {}
  virtual void OnMessage(Message* msg) { (void)msg; result_ = std::move(functor_()); }
  std::unique_ptr<ReturnT> result() { return std::move(result_); }
  
 private:
  FunctorT functor_;
  std::unique_ptr<ReturnT> result_;
};

template <class FunctorT>
class FunctorMessageHandler<void, FunctorT> : public MessageHandler {
 public:
  explicit FunctorMessageHandler(const FunctorT& functor)
     : functor_(functor) {}
  virtual void OnMessage(Message* msg) {
    (void)msg;
     functor_();
  }

  void result() const {}
  
 private:
  FunctorT functor_;
};


} // namespace net
#endif // NET_MESSAGE_HANDLER_H_
