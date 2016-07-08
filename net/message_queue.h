#ifndef NET_MESSAGE_QUEUE_H_
#define NET_MESSAGE_QUEUE_H_
#include "base/macros.h"
#include "base/critical_section.h"
#include "base/location.h"
#include "base/scoped_ref_ptr.h"
#include "base/time.h"

#include "net/sharedexclusivelock.h"
#include "net/message_handler.h"
#include "net/sigslot.h"
#include "net/socket_server.h"

#include <string.h>

#include <algorithm>
#include <list>
#include <memory>
#include <queue>
#include <vector>

namespace net {

struct Message;
class MessageQueue;

// MessageQueueManager does cleanup of message queues
class MessageQueueManager {
 public:
  static void Add(MessageQueue *message_queue);
  static void Remove(MessageQueue *message_queue);
  static void Clear(MessageHandler *handler);

  static bool IsInitialized();
  static void ProcessAllMessageQueues();

 private:
  static MessageQueueManager* Instace();
  
  MessageQueueManager();
  ~MessageQueueManager();

  void AddInternal(MessageQueue* message_queue);
  void RemoveInternal(MessageQueue* message_queue);
  void ClearInternal(MessageHandler *handler);
  void ProcessAllMessageQueuesInternal();

  static MessageQueueManager* instance_;
  std::vector<MessageQueue *>  message_queue_;
  base::CriticalSection crit_;
};

class MessageData {
 public:
  MessageData() {}
  virtual ~MessageData() {}
};

template <class T>
class TypedMessageData : public MessageData {
 public:
  explicit TypedMessageData(const T& data) : data_(data) { }
  const T& data() const { return data_; }
  T& data() { return data_; }
 private:
  T data_;
};

template <class T>
class ScopedMessageData : public MessageData {
 public:
  explicit ScopedMessageData(T* data) : data_(data) { }
  const std::unique_ptr<T>& data() const { return data_; }
  std::unique_ptr<T>& data() { return data_; }

 private:
  std::unique_ptr<T> data_;
};

template <class T>
class ScopedRefMessageData : public MessageData {
 public:
  explicit ScopedRefMessageData(T* data) : data_(data) { }
  const base::scoped_refptr<T>& data() const { return data_; }
  base::scoped_refptr<T>& data() { return data_; }
 private:
  base::scoped_refptr<T> data_;
};

template<class T>
inline MessageData* WrapMessageData(const T& data) {
  return new TypedMessageData<T>(data);
}

template<class T>
inline const T& UseMessageData(MessageData* data) {
  return static_cast< TypedMessageData<T>* >(data)->data();
}

template<class T>
class DisposeData : public MessageData {
 public:
  explicit DisposeData(T* data) : data_(data) { }
  virtual ~DisposeData() { delete data_; }
 private:
  T* data_;
};

const uint32_t MQID_ANY = static_cast<uint32_t>(-1);
const uint32_t MQID_DISPOSE = static_cast<uint32_t>(-2);

struct Message {
  Message()
      : phandler(nullptr), message_id(0), pdata(nullptr), ts_sensitive(0) {}
  inline bool Match(MessageHandler* handler, uint32_t id) const {
    return (handler == NULL || handler == phandler)
           && (id == MQID_ANY || id == message_id);
  }
  base::Location posted_from;
  MessageHandler *phandler;
  uint32_t message_id;
  MessageData *pdata;
  int64_t ts_sensitive;
};

typedef std::list<Message> MessageList;

class DelayedMessage {
 public:
  DelayedMessage(int64_t delay,
                 int64_t trigger,
                 uint32_t num,
                 const Message& msg)
      : cmsDelay_(delay), msTrigger_(trigger), num_(num), msg_(msg) {}

  bool operator< (const DelayedMessage& dmsg) const {
    return (dmsg.msTrigger_ < msTrigger_)
           || ((dmsg.msTrigger_ == msTrigger_) && (dmsg.num_ < num_));
  }

  int64_t cmsDelay_;  // for debugging
  int64_t msTrigger_;
  uint32_t num_;
  Message msg_;
};

///////////////// MessageQueue
class MessageQueue {
 public:
  static const int kForever = -1;
  
  MessageQueue(SocketServer* ss, bool init_queue);
  MessageQueue(std::unique_ptr<SocketServer> ss, bool init_queue);

  virtual ~MessageQueue();

  SocketServer* socketserver();
  void set_socketserver(SocketServer* ss);

  virtual void Quit();
  virtual bool IsQuitting();
  virtual void Restart();

  virtual bool Get(Message *pmsg, int cmsWait = kForever,
                   bool process_io = true);
  virtual bool Peek(Message *pmsg, int cmsWait = 0);
  virtual void Post(const base::Location& posted_from,
                    MessageHandler* phandler,
                    uint32_t id = 0,
                    MessageData* pdata = NULL,
                    bool time_sensitive = false);
  virtual void PostDelayed(const base::Location& posted_from,
                           int cmsDelay,
                           MessageHandler* phandler,
                           uint32_t id = 0,
                           MessageData* pdata = NULL);
  virtual void PostAt(const base::Location& posted_from,
                      int64_t tstamp,
                      MessageHandler* phandler,
                      uint32_t id = 0,
                      MessageData* pdata = NULL);
  virtual void PostAt(const base::Location& posted_from,
                      uint32_t tstamp,
                      MessageHandler* phandler,
                      uint32_t id = 0,
                      MessageData* pdata = NULL);
  virtual void Clear(MessageHandler* phandler,
                     uint32_t id = MQID_ANY,
                     MessageList* removed = NULL);
  virtual void Dispatch(Message *pmsg);
  virtual void ReceiveSends();
  
  virtual int GetDelay();

  bool empty() const { return size() == 0u; }
  size_t size() const {
base::CritScope cs(&crit_);  // msgq_.size() is not thread safe.
    return msgq_.size() + dmsgq_.size() + (fPeekKeep_ ? 1u : 0u);
  }


  template<class T> void Dispose(T* doomed) {
    if (doomed) {
      Post(FROM_HERE, NULL, MQID_DISPOSE, new DisposeData<T>(doomed));
    }
  }

  sigslot::signal0<> SignalQueueDestroyed;


 protected:
  class PriorityQueue : public std::priority_queue<DelayedMessage> {
   public:
    container_type& container() { return c; }
    void reheap() { make_heap(c.begin(), c.end(), comp); }
  };

  void DoDelayPost(const base::Location& posted_from,
                   int64_t cmsDelay,
                   int64_t tstamp,
                   MessageHandler* phandler,
                   uint32_t id,
                   MessageData* pdata);
  void DoInit();
  void DoDestroy();
  void WakeUpSocketServer();
  
  bool fStop_;
  bool fPeekKeep_;
  Message msgPeek_;
  MessageList msgq_; // GUARDED_BY(crit_);
  PriorityQueue dmsgq_; // GUARDED_BY(crit_);
  uint32_t dmsgq_next_num_; // GUARDED_BY(crit_);
  base::CriticalSection crit_;
  bool fInitialized_;
  bool fDestroyed_;

 private:
  SocketServer* ss_;
  std::unique_ptr<SocketServer> own_ss_;
  SharedExclusiveLock ss_lock_;

  //TODO
  // DISALLOW_IMPLICIT_CONSTRUCTORS(MessageQueue)

};



} // namespace net
#endif // NET_MESSAGE_QUEUE_H_
