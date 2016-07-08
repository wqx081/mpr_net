#include "net/message_handler.h"
#include "net/message_queue.h"

namespace net {

MessageHandler::~MessageHandler() {
  MessageQueueManager::Clear(this);
}

} // namespace net
