#include "fb/io_buffer.h"

#include <stdexcept>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

using std::unique_ptr;


namespace {

enum : uint16_t {
  kHeapMagic = 0xa5a5,
  kIOBufferInUse = 0x01,
  kDataInUse = 0x02,
};

enum : uint64_t {
  kDefaultCombinedBufferSize = 1024
};

void TakeOwnershipError(bool free_on_error,
                        void* buf,
                        fb::IOBuffer::FreeFunction free_fn,
                        void* user_data) {
  if (!free_on_error) {
    return;
  }
  if (!free_fn) {
    free(buf);
    return;
  }
  try {
    free_fn(buf, user_data);
  } catch (...) {
    abort();
  }
}

} // namespace


namespace fb {


struct IOBuffer::HeapPrefix {
  HeapPrefix(uint16_t in_flags) : magic(kHeapMagic), flags(in_flags) {
  }
  ~HeapPrefix() {
    magic = 0;
  }
  
  uint16_t magic;
  std::atomic<uint16_t> flags;
};
  
struct IOBuffer::HeapStorage {
  HeapPrefix prefix;
  IOBuffer buffer;
};
  
struct IOBuffer::HeapFullStorage {
  static_assert(sizeof(HeapStorage) <= 64,
                "IOBuffer may not grow over 56 bytes!");
  
  HeapStorage hs;
  SharedInfo shared;
  MaxAlign align;
};

IOBuffer::IOBuffer(CreateOperator, uint64_t capacity)
    : next_(this),
      prev_(this),
      data_(nullptr),
      length_(0),
      flags_and_shared_info_(0) {
  SharedInfo* info;
  AllocateExtendBuffer(capacity, &buf_, &info, &capacity_);
  SetSharedInfo(info);
  data_ = buf_;
}

IOBuffer::IOBuffer(CopyBufferOperator op,
                   const void* buf,
                   uint64_t size,
                   uint64_t headroom,
                   uint64_t min_tailroom)
    : IOBuffer(CREATE, headroom + size + min_tailroom) {
  (void) op;
  Advance(headroom);
  memcpy(WritableData(), buf, size);
  Append(size);
}

unique_ptr<IOBuffer> IOBuffer::Create(uint64_t capacity) {
  if (capacity <= kDefaultCombinedBufferSize) {
    return CreateCombined(capacity);
  }
  return CreateSeparate(capacity);
}

unique_ptr<IOBuffer> IOBuffer::CreateCombined(uint64_t capacity) {
  size_t required_storage = offsetof(HeapFullStorage, align) + capacity;
  size_t malloc_size = GoodMallocSize(required_storage);  
  auto* storage = static_cast<HeapFullStorage *>(malloc(malloc_size));
  
  new(&storage->hs.prefix) HeapPrefix(kIOBufferInUse | kDataInUse);
  new(&storage->shared) SharedInfo(FreeInternalBuffer, storage);
  
  uint8_t* buf_addr = reinterpret_cast<uint8_t *>(&storage->align);
  uint8_t* storage_end = reinterpret_cast<uint8_t *>(storage) + malloc_size;
  size_t actual_capacity = storage_end - buf_addr;
  unique_ptr<IOBuffer> ret(new (&storage->hs.buffer) IOBuffer(
      InternalConstructor(), 
      PackFlagsAndSharedInfo(0, &storage->shared),
      buf_addr,
      actual_capacity,
      buf_addr,
      0));
  return ret;
}

unique_ptr<IOBuffer> IOBuffer::CreateSeparate(uint64_t capacity) {
  return make_unique<IOBuffer>(CREATE, capacity);
}

unique_ptr<IOBuffer> IOBuffer::CreateChain(size_t total_capacity,
                                           uint64_t max_buf_capacity) {
  unique_ptr<IOBuffer> ret = Create(std::min(total_capacity, size_t(max_buf_capacity)));
  size_t allocated_capacity = ret->capacity();
  while (allocated_capacity < total_capacity) {
    unique_ptr<IOBuffer> new_buf = Create(std::min(total_capacity - allocated_capacity,
                                                   size_t(max_buf_capacity)));
    allocated_capacity += new_buf->capacity();
    ret->PrependChain(std::move(new_buf));
  }
  return ret;
}

IOBuffer::IOBuffer(TakeOwnershipOperator,
                   void* buf,
                   uint64_t capacity,
                   uint64_t length,
                   FreeFunction free_fn,
                   void* user_data,
                   bool free_on_error)
    : next_(this),
      prev_(this),
      data_(static_cast<uint8_t*>(buf)),
      buf_(static_cast<uint8_t*>(buf)),
      length_(length),
      capacity_(capacity),
      flags_and_shared_info_(PackFlagsAndSharedInfo(kFlagsFreeSharedInfo, nullptr)) {
  try {
    SetSharedInfo(new SharedInfo(free_fn, user_data));
  } catch (...) {
    TakeOwnershipError(free_on_error, buf, free_fn, user_data);
    throw;
  }
}

unique_ptr<IOBuffer> IOBuffer::TakeOwnership(void* buf,
                                             uint64_t capacity,
                                             uint64_t length,
                                             FreeFunction free_fn,
                                             void* user_data,
                                             bool free_on_error) {
  try {
    return make_unique<IOBuffer>(TAKE_OWNERSHIP,
                                 buf,
                                 capacity,
                                 length,
                                 free_fn,
                                 user_data,
                                 false);
  } catch (...) {
    TakeOwnershipError(free_on_error, buf, free_fn, user_data);
    throw;
  }
}

IOBuffer::IOBuffer(WrapBufferOperator,
                   const void* buf,
                   uint64_t capacity) 
    : IOBuffer(InternalConstructor(),
               0,
               static_cast<uint8_t*>(const_cast<void*>(buf)),
               capacity,
               static_cast<uint8_t*>(const_cast<void*>(buf)),
               capacity) {
}

unique_ptr<IOBuffer> IOBuffer::WrapBuffer(const void* buf,
                                          uint64_t capacity) {
  return make_unique<IOBuffer>(WRAP_BUFFER, buf, capacity);
}

IOBuffer::IOBuffer() noexcept {
}

IOBuffer::IOBuffer(IOBuffer&& other) noexcept {
  *this = std::move(other);
}

IOBuffer::IOBuffer(InternalConstructor,
                   uintptr_t flags_and_shared_info,
                   uint8_t* buf,
                   uint64_t capacity,
                   uint8_t* data,
                   uint64_t length)
    : next_(this),
      prev_(this),
      data_(data),
      buf_(buf),
      length_(length),
      capacity_(capacity),
      flags_and_shared_info_(flags_and_shared_info) {
  assert(data >= buf);
  assert(data + length <= buf + capacity);
}
  
IOBuffer::~IOBuffer() {
  while (next_ != this) {
    (void)next_->Unlink();
  } 
  DecrementRefcount();
} 

IOBuffer& IOBuffer::operator=(IOBuffer&& other) noexcept {
  if (this == &other) {
    return *this;
  }
  while (next_ != this) {
    (void) next_->Unlink();
  }
  DecrementRefcount();

  data_ = other.data_;
  buf_ = other.buf_;
  length_ = other.length_;
  capacity_ = other.capacity_;
  flags_and_shared_info_ = other.flags_and_shared_info_;
  other.data_ = nullptr;
  other.buf_ = nullptr;
  other.length_ = 0;
  other.capacity_ = 0;
  other.flags_and_shared_info_ = 0;

  if (other.next_ != &other) {
    next_ = other.next_;
    next_->prev_ = this;
    other.next_ = &other;
  
    prev_ = other.prev_;
    prev_->next_ = this;
    other.prev_ = &other;
  }

  DCHECK_EQ(other.prev_, &other);
  DCHECK_EQ(other.next_, &other);

  return *this;
}

bool IOBuffer::empty() const {
  const IOBuffer* current = this;
  do {
    if (current->length() != 0) {
      return false;
    }
    current = current->next_;
  } while (current != this);
  return true;
}
  
size_t IOBuffer::CountChainElements() const {
  size_t num_elements = 1;
  for (IOBuffer* current = next_; current != this; current = current->next_) {
    ++num_elements;
  }
  return num_elements;
}
  
uint64_t IOBuffer::ComputeChainDataLength() const {
  uint64_t full_length = length_;
  for (IOBuffer* current = next_; current != this; current = current->next_) {
    full_length += current->length_;
  }
  return full_length;
}

void IOBuffer::PrependChain(unique_ptr<IOBuffer>&& iobuf) {
  IOBuffer* other = iobuf.release();
  IOBuffer* other_tail = other->prev_;

  prev_->next_ = other;
  other->prev_ = prev_;

  other_tail->next_ = this;
  prev_ = other_tail;
}

unique_ptr<IOBuffer> IOBuffer::Clone() const {
  unique_ptr<IOBuffer> ret = make_unique<IOBuffer>();
  CloneInto(*ret);
  return ret;
}

unique_ptr<IOBuffer> IOBuffer::CloneOne() const {
  unique_ptr<IOBuffer> ret = make_unique<IOBuffer>();
  CloneOneInto(*ret);
  return ret;
}

void IOBuffer::CloneInto(IOBuffer& other) const {
  IOBuffer tmp;
  CloneOneInto(tmp);

  for (IOBuffer* current = next_; current != this; current = current->next_) {
    tmp.PrependChain(current->CloneOne());
  }
  other = std::move(tmp);
}

void IOBuffer::CloneOneInto(IOBuffer& other) const {
  SharedInfo* info = GetSharedInfo();
  if (info) {
    SetFlags(kFlagsMaybeShared);
  }
  other = IOBuffer(InternalConstructor(),
                   flags_and_shared_info_,
                   buf_,
                   capacity_,
                   data_,
                   length_);
  if (info) {
    info->refcount.fetch_add(1, std::memory_order_acq_rel);
  }
}

void IOBuffer::DoUnsharedOne() {
  uint8_t* buf;
  SharedInfo* shared_info;
  uint64_t actual_capacity;
  AllocateExtendBuffer(capacity_, &buf, &shared_info, &actual_capacity);
  
  uint64_t headlen = Headroom();
  memcpy(buf + headlen, data_, length_);
    
  DecrementRefcount();
  SetFlagsAndSharedInfo(0, shared_info);
    
  data_ = buf + headlen;
  buf_ = buf; 
} 
  
void IOBuffer::UnsharedChained() {
  assert(IsChained());
    
  IOBuffer* current = this;
  while (true) { 
  if (current->IsSharedOne()) {
    break;
  } 
                        
  current = current->next_;
  if (current == this) {
    return;
  } 
                      } 
  DoMerge();
} 

void IOBuffer::DoMerge() {
  DCHECK(IsChained());
  uint64_t new_length = 0;
  IOBuffer* end = this;
  do {
    new_length += end->length_;
    end = end->next_;
  } while (end != this);
  MergeAndReallocate(new_length, end);
  DCHECK(!IsChained());
}

void IOBuffer::DoMerge(size_t max_length) {
  DCHECK(IsChained());
  DCHECK_LE(length_, max_length);

  uint64_t new_length = 0;
  IOBuffer* end = this;
  while (true) {
    new_length += end->length_;
    end = end->next_;
    if (new_length >= max_length) {
      break;
    } 
    if (end == this) {
      throw std::overflow_error("attempted to coalesce more data than "
                                "available");
    }          
  }
  
  MergeAndReallocate(new_length, end);
  DCHECK_GE(length_, max_length);
}

void IOBuffer::MergeAndReallocate(size_t new_headroom,
                                  size_t new_length,
                                  IOBuffer* end,
                                  size_t new_tailroom) {
  uint64_t new_capacity = new_length + new_headroom + new_tailroom;  
  uint8_t* new_buf;
  SharedInfo* new_info;
  uint64_t actual_capacity;
  AllocateExtendBuffer(new_capacity, 
                       &new_buf,
                       &new_info,
                       &actual_capacity);
  uint8_t* new_data = new_buf + new_headroom;
  uint8_t* p = new_data;
  IOBuffer* current = this;
  size_t remaining = new_length;
  do {
    assert(current->length_ <= remaining);
    remaining -= current->length_;
    memcpy(p, current->data_, current->length_);
    p += current->length_;
    current = current->next_;
  } while (current != end);
  assert(remaining == 0);

  DecrementRefcount();
  
  SetFlagsAndSharedInfo(0, new_info);

  capacity_ = actual_capacity;
  buf_ = new_buf;
  data_ = new_data;
  length_ = new_length;

  if (IsChained()) {
    (void)SeparateChain(next_, current->prev_);
  }
}

void IOBuffer::DecrementRefcount() {
  SharedInfo* info = GetSharedInfo();
  if (!info) {
    return;
  }

  uint32_t new_cnt = info->refcount.fetch_sub(1,
                                              std::memory_order_acq_rel);
  if (new_cnt > 1) {
    return;
  }

  FreeExtendBuffer();

  if (flags() & kFlagsFreeSharedInfo) {
    delete GetSharedInfo();
  }
}

void IOBuffer::DoReserve(uint64_t min_headroom,
                         uint64_t min_tailroom) {
  size_t new_capacity = (size_t)length_ + min_headroom + min_tailroom;
  DCHECK_LE(new_capacity, UINT32_MAX);

  DCHECK(!IsSharedOne());

  if (Headroom() + Tailroom() >= min_headroom + min_tailroom) {
    uint8_t* new_data = WritableBuffer() + min_headroom;
    memmove(new_data, data_, length_);
    data_ = new_data;
    return;
  }
  size_t new_allocated_capacity = GoodExtendBufferSize(new_capacity);
  uint8_t* new_buffer = nullptr;
  uint64_t new_headroom = 0;
  uint64_t old_headroom = Headroom();

  SharedInfo* info = GetSharedInfo();
  if (info && (info->free_fn == nullptr) && length_ != 0 &&
      old_headroom >= min_headroom) {
    size_t copy_slack = capacity() - length_;
    if (copy_slack * 2 <= length_) {
      void* p = realloc(buf_, new_allocated_capacity);
      if (MPR_UNLIKELY(p == nullptr)) {
        throw std::bad_alloc();
      }
      new_buffer = static_cast<uint8_t *>(p);
      new_headroom = old_headroom;
    }
  }
  
  if (new_buffer == nullptr) {
    void* p = malloc(new_allocated_capacity);
    if (MPR_UNLIKELY(p == nullptr)) {
      throw std::bad_alloc();
    }
    new_buffer = static_cast<uint8_t *>(p);
    memcpy(new_buffer + min_headroom, data_, length_);
    if (GetSharedInfo()) {
      FreeExtendBuffer(); 
    }
    new_headroom = min_headroom;
  }
  
  uint64_t cap;
  InitExtendBuffer(new_buffer, new_allocated_capacity, &info, &cap);

  if (flags() & kFlagsFreeSharedInfo) {
    delete GetSharedInfo();
  }

  SetFlagsAndSharedInfo(0, info);
  capacity_ = cap;
  buf_ = new_buffer;
  data_ = new_buffer + new_headroom;
}

std::vector<struct iovec> IOBuffer::GetIov() const {
  std::vector<struct iovec> iov;
  iov.reserve(CountChainElements());
  AppendToIov(&iov);
  return iov;
}

void IOBuffer::AppendToIov(std::vector<struct iovec>* iov) const {
  const IOBuffer* p = this;
  do {
    if (p->length() > 0) {
      iov->push_back({(void*)p->data(), size_t(p->length())});
    }
    p = p->next();
  } while (p != this);
}

size_t IOBuffer::FillIov(struct iovec* iov, size_t len) const {
  const IOBuffer* p = this;
  size_t i = 0;
  while (i < len) {
    if (p->length() > 0) {
      iov[i].iov_base = const_cast<uint8_t *>(p->data());
      iov[i].iov_len = p->length();
      i++;
    }
    p = p->next();
    if (p == this) {
      return i;
    }
  }
  return 0;
}

// Memory manager

IOBuffer::SharedInfo::SharedInfo()
    : free_fn(nullptr),
      user_data(nullptr) {
  refcount.store(1, std::memory_order_relaxed);
}

IOBuffer::SharedInfo::SharedInfo(FreeFunction in_free_fn,
                                 void* in_user_data)
    : free_fn(in_free_fn),
      user_data(in_user_data) {
  refcount.store(1, std::memory_order_relaxed);
}

void* IOBuffer::operator new(size_t size) {
  size_t full_size = offsetof(HeapStorage, buffer) + size;
  auto* storage = static_cast<HeapStorage *>(malloc(full_size));
  if (MPR_UNLIKELY(storage == nullptr)) {
    throw std::bad_alloc();
  }
  new (&storage->prefix) HeapPrefix(kIOBufferInUse);
  return &(storage->buffer);
}

void* IOBuffer::operator new(size_t size, void* ptr) {
  (void) size;
  return ptr;
}

void IOBuffer::operator delete(void* ptr) {
  auto* storage_addr = static_cast<uint8_t*>(ptr) - offsetof(HeapStorage, buffer);
  auto* storage = reinterpret_cast<HeapStorage *>(storage_addr);
  ReleaseStorage(storage, kIOBufferInUse);
}

void IOBuffer::ReleaseStorage(HeapStorage* storage,
                              uint16_t free_flags) {
  CHECK_EQ(storage->prefix.magic, static_cast<uint16_t>(kHeapMagic));
  
  auto flags = storage->prefix.flags.load(std::memory_order_acquire);
  DCHECK_EQ((flags & free_flags), free_flags);
    
  while (true) {
    uint16_t newFlags = (flags & ~free_flags);
    if (newFlags == 0) {
      storage->prefix.HeapPrefix::~HeapPrefix();
      free(storage);
      return;
    } 
      
    auto ret = storage->prefix.flags.compare_exchange_weak(
          flags, newFlags, std::memory_order_acq_rel);
    if (ret) { 
      return;
    } 
      
  } 
}

void IOBuffer::FreeInternalBuffer(void* buf, void* user_data) {
  (void) buf;
  auto* storage = static_cast<HeapStorage *>(user_data);
  ReleaseStorage(storage, kDataInUse);
}


////////////////////////////////


// Extend Buffer
//
void IOBuffer::FreeExtendBuffer() {
  SharedInfo* info = GetSharedInfo();
  DCHECK(info);

  if (info->free_fn) {
    try {
      info->free_fn(buf_, info->user_data);
    } catch (...) {
      abort();
    }
  } else {
    free(buf_);
  } 
}

void IOBuffer::AllocateExtendBuffer(uint64_t min_capacity,
                                    uint8_t** buf_return,
                                    SharedInfo** info_return,
                                    uint64_t* capacity_return) {
  size_t malloc_size = GoodExtendBufferSize(min_capacity);
  uint8_t* buf = static_cast<uint8_t*>(malloc(malloc_size));
  if (MPR_UNLIKELY(buf == nullptr)) {
    throw std::bad_alloc();
  }
  InitExtendBuffer(buf, malloc_size, info_return, capacity_return);
  *buf_return = buf;
}

size_t IOBuffer::GoodExtendBufferSize(uint64_t min_capacity) {
  size_t min_size = static_cast<size_t>(min_capacity) + sizeof(SharedInfo);
  min_size = (min_size + 7) & ~7;
  return GoodMallocSize(min_size);
}

void IOBuffer::InitExtendBuffer(uint8_t* buf,
                                size_t malloc_size,
                                SharedInfo** info_return,
                                uint64_t* capacity_return) {
  uint8_t* info_start = (buf + malloc_size) - sizeof(SharedInfo);
  SharedInfo* shared_info = new(info_start) SharedInfo;

  *capacity_return = info_start - buf;
  *info_return = shared_info;
}



} // namespace fb
