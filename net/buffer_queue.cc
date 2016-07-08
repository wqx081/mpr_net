#include "net/buffer_queue.h"

#include <algorithm>

using namespace base;

namespace net {

BufferQueue::BufferQueue(size_t capacity, size_t default_size)
    : capacity_(capacity), default_size_(default_size) {
}

BufferQueue::~BufferQueue() {
  CritScope cs(&crit_);

  for (Buffer* buffer : queue_) {
    delete buffer;
  }
  for (Buffer* buffer : free_list_) {
    delete buffer;
  }
}

size_t BufferQueue::size() const {
  CritScope cs(&crit_);
  return queue_.size();
}

void BufferQueue::Clear() {
  CritScope cs(&crit_);
  while (!queue_.empty()) {
    free_list_.push_back(queue_.front());
    queue_.pop_front();
  }
}

bool BufferQueue::ReadFront(void* buffer, size_t bytes, size_t* bytes_read) {
  CritScope cs(&crit_);
  if (queue_.empty()) {
    return false;
  }

  bool was_writable = queue_.size() < capacity_;
  Buffer* packet = queue_.front();
  queue_.pop_front();

  bytes = std::min(bytes, packet->size());
  memcpy(buffer, packet->data(), bytes);
  if (bytes_read) {
    *bytes_read = bytes;
  }
  free_list_.push_back(packet);
  if (!was_writable) {
    NotifyWritableForTest();
  }
  return true;
}

bool BufferQueue::WriteBack(const void* buffer, size_t bytes,
                            size_t* bytes_written) {
  CritScope cs(&crit_);
  if (queue_.size() == capacity_) {
    return false;
  }

  bool was_readable = !queue_.empty();
  Buffer* packet;
  if (!free_list_.empty()) {
    packet = free_list_.back();
    free_list_.pop_back();
  } else {
    packet = new Buffer(bytes, default_size_);
  }

  packet->SetData(static_cast<const uint8_t*>(buffer), bytes);
  if (bytes_written) {
    *bytes_written = bytes;
  }
  queue_.push_back(packet);
  if (!was_readable) {
    NotifyReadableForTest();
  }
  return true;
}

}  // namespace rtc
