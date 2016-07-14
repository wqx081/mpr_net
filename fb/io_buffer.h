#ifndef FB_IO_BUFFER_H_
#define FB_IO_BUFFER_H_

#include "base/macros.h"

#include <glog/logging.h>
#include <atomic>
#include <cassert>
#include <cinttypes>
#include <cstddef>
#include <cstring>
#include <memory>
#include <limits>
#include <sys/uio.h>
#include <type_traits>

#include <boost/iterator/iterator_facade.hpp>

// Coalesce -> Merge

namespace fb {

namespace {

template<class T, class Enable=void> 
struct IsUniquePtrToSL : public std::false_type { 
};

template<class T, class D>
struct IsUniquePtrToSL<
  std::unique_ptr<T, D>,
  typename std::enable_if<std::is_standard_layout<T>::value>::type>
  : public std::true_type { };

} // namespace

class IOBuffer {
 public:
  enum CreateOperator { CREATE };
  enum WrapBufferOperator { WRAP_BUFFER };
  enum TakeOwnershipOperator { TAKE_OWNERSHIP };
  enum CopyBufferOperator { COPY_BUFFER };

  typedef void (*FreeFunction)(void* buf, void* user_data);

  static std::unique_ptr<IOBuffer> Create(uint64_t capacity);
  IOBuffer(CreateOperator, uint64_t capacity);
  static std::unique_ptr<IOBuffer> CreateCombined(uint64_t capacity);
  static std::unique_ptr<IOBuffer> CreateSeparate(uint64_t capacity);
  static std::unique_ptr<IOBuffer> CreateChain(size_t total_capacity, uint64_t max_buf_capacity);

  static std::unique_ptr<IOBuffer> TakeOwnership(void* buf,
                                                 uint64_t capacity,
                                                 uint64_t length,
                                                 FreeFunction free_fn = nullptr,
                                                 void* user_data = nullptr,
                                                 bool free_on_error = true);
  IOBuffer(TakeOwnershipOperator op, 
           void* buf, 
           uint64_t capacity,
           uint64_t length,
           FreeFunction free_fn = nullptr,
           void* user_data = nullptr,
           bool free_on_error = true);
  template<class UniquePtr>
  static typename std::enable_if<IsUniquePtrToSL<UniquePtr>::value, 
                                 std::unique_ptr<IOBuffer>>::type
  TakeOwnership(UniquePtr&& buf, size_t count=1);
  
  static std::unique_ptr<IOBuffer> WrapBuffer(const void* buf, uint64_t capacity);
  IOBuffer(WrapBufferOperator op, const void* buf, uint64_t capacity);

  static std::unique_ptr<IOBuffer> CopyBuffer(const void* buf,
                                              uint64_t size,
                                              uint64_t headroom = 0,
                                              uint64_t min_tailroom = 0);
  IOBuffer(CopyBufferOperator op, 
           const void* buf, 
           uint64_t size,
           uint64_t headroom = 0,
           uint64_t min_tailroom = 0);

  static std::unique_ptr<IOBuffer> CopyBuffer(const std::string& buf,
                                              uint64_t headroom = 0,
                                              uint64_t min_tailroom = 0);
  IOBuffer(CopyBufferOperator op,
           const std::string& buf,
           uint64_t headroom = 0,
           uint64_t min_tailroom = 0);
  static std::unique_ptr<IOBuffer> MaybeCopyBuffer(const std::string& buf,
                                                   uint64_t headroom = 0,
                                                   uint64_t min_tailroom = 0);

  static void Destroy(std::unique_ptr<IOBuffer>&& data) {
    auto destroyer = std::move(data);
  }
  
  ~IOBuffer();

  //
  bool empty() const;
  const uint8_t* data() const { 
    return data_;
  }

  uint8_t* WritableData() {
    return data_;
  }

  const uint8_t* Tail() const {
    return data_ + length_;
  }

  uint8_t* WritableTail() {
    return data_ + length_;
  }

  uint64_t length() const {
    return length_;
  }

  uint64_t Headroom() const {
    return data_ - buffer();
  }

  uint64_t Tailroom() const {
    return BufferEnd() - Tail();
  }

  const uint8_t* buffer() const {
    return buf_;
  }

  uint8_t* WritableBuffer() {
    return buf_;
  }

  const uint8_t* BufferEnd() const {
    return buf_ + capacity_;
  }

  uint64_t capacity() const {
    return capacity_;
  }

  IOBuffer* next() {
    return next_;
  }

  const IOBuffer* next() const {
    return next_;
  }

  IOBuffer* prev() {
    return prev_;
  }

  const IOBuffer* prev() const {
    return prev_;
  }

  void Advance(uint64_t amount) {
    assert(amount <= Tailroom());
    if (length_ > 0) {
      memmove(data_ + amount, data_, length_);
    }
    data_ += amount;
  }

  void Retreat(uint64_t amount) {
    assert(amount <= Headroom());
    if (length_ > 0) {
      memmove(data_ - amount, data_, length_);
    }
    data_ -= amount;
  }

  void Prepend(uint64_t amount) {
    DCHECK_LE(amount, Headroom());
    data_ -= amount;
    length_ += amount;
  }
  
  void Append(uint64_t amount) {
    DCHECK_LE(amount, Tailroom());
    length_ += amount;
  }

  void TrimStart(uint64_t amount) {
    DCHECK_LE(amount, length_);
    data_ += amount;
    length_ -= amount;
  }

  void TrimEnd(uint64_t amount) {
    DCHECK_LE(amount, length_);
    length_ -= amount;
  }
  
  void Clear() {
    data_ = WritableBuffer();
    length_ = 0;
  }

  void Reserve(uint64_t min_headroom, uint64_t min_tailroom) {
    if (Headroom() >= min_headroom && Tailroom() >= min_tailroom) {
      return;
    }
    if (length() == 0 &&
        Headroom() + Tailroom() >= min_headroom + min_tailroom) {
      data_ = WritableBuffer() + min_headroom;
      return;
    }
    DoReserve(min_headroom, min_tailroom);
  }

  bool IsChained() const {
    assert((next_ == this) == (prev_ == this));
    return next_ != this;
  }

  size_t CountChainElements() const;
  uint64_t ComputeChainDataLength() const;
  void PrependChain(std::unique_ptr<IOBuffer>&& io_buffer);
  void AppendChain(std::unique_ptr<IOBuffer>&& io_buffer) {
    next_->PrependChain(std::move(io_buffer));
  }
  
  std::unique_ptr<IOBuffer> Unlink() {
    next_->prev_ = prev_;
    prev_->next_ = next_;
    prev_ = this;
    next_ = this;
    return std::unique_ptr<IOBuffer>(this);
  }

  std::unique_ptr<IOBuffer> Pop() {
    IOBuffer* next = next_;
    next_->prev_ = prev_;
    prev_->next_ = next_;
    prev_ = this;
    next_ = this;
    return std::unique_ptr<IOBuffer>((next == this) ? nullptr : next);
  }

  std::unique_ptr<IOBuffer> SeparateChain(IOBuffer* head, IOBuffer* tail) {
    assert(head != this);
    assert(tail != this);

    head->prev_->next_ = tail->next_;
    tail->next_->prev_ = head->prev_;

    head->prev_ = tail;
    tail->next_ = head;

    return std::unique_ptr<IOBuffer>(head);
  }
  
  bool IsShared() const {
    const IOBuffer* current = this;
    while (true) {
      if (current->IsSharedOne()) {
        return true;
      }
      current = current->next_;
      if (current == this) {
        return false;
      }
    }
  }

  bool IsSharedOne() const {
    if (MPR_UNLIKELY(!GetSharedInfo())) {
      return true;
    }
    if (MPR_LIKELY(!(flags() & kFlagsMaybeShared))) {
      return false;
    }
    bool shared = GetSharedInfo()->refcount.load(std::memory_order_acquire) > 1;
    if (!shared) {
      ClearFlags(kFlagsMaybeShared);
    }
    return shared;
  }

  void Unshared() {
    if (IsChained()) {
      UnsharedChained();  
    } else {
      UnsharedOne();
    }
  }

  void UnsharedOne() {
    if (IsSharedOne()) {
      DoUnsharedOne();
    }
  }
  
  void Gather(uint64_t max_length) {
    if (!IsChained() || length_ >= max_length) {
      return;
    }
    DoMerge(max_length);
  }

  std::unique_ptr<IOBuffer> Clone() const;
  std::unique_ptr<IOBuffer> CloneOne() const;
  void CloneInto(IOBuffer& o) const;
  void CloneOneInto(IOBuffer& o) const;

  std::vector<struct iovec> GetIov() const;
  void AppendToIov(std::vector<struct iovec>* iov) const;
  size_t FillIov(struct iovec* iov, size_t len) const;

  void* operator new(size_t size);
  void* operator new(size_t size, void* ptr);
  void operator delete(void* ptr);

  
  //
  IOBuffer() noexcept;
  IOBuffer(IOBuffer&& other) noexcept;
  IOBuffer& operator=(IOBuffer&& other) noexcept;

 private:
  enum FlagsEnum : uintptr_t {
    kFlagsFreeSharedInfo = 0x1,
    kFlagsMaybeShared = 0x2,
    kFlagsMask = (kFlagsFreeSharedInfo | kFlagsMaybeShared)
  };
  
  struct SharedInfo {
    SharedInfo();
    SharedInfo(FreeFunction fn, void* arg);

    FreeFunction free_fn;
    void* user_data;
    std::atomic<uint32_t> refcount;
  };
  struct HeapPrefix;
  struct HeapStorage;
  struct HeapFullStorage;
  
  struct InternalConstructor {};  
  IOBuffer(InternalConstructor,
           uintptr_t flags_shared_info,
           uint8_t* buf,
           uint64_t capacity,
           uint8_t* data,
           uint64_t length);
  void DoUnsharedOne();
  void UnsharedChained();
  void DoMerge();
  void DoMerge(size_t max_length);
  void MergeAndReallocate(size_t headroom,
                          size_t length,
                          IOBuffer* end,
                          size_t Tailroom);
  void MergeAndReallocate(size_t length, IOBuffer* end) {
    MergeAndReallocate(Headroom(), length, end, end->prev_->Tailroom());
  }

  void DecrementRefcount();
  void DoReserve(uint64_t min_headroom, uint64_t min_tailroom);
  void FreeExtendBuffer();

  static size_t GoodExtendBufferSize(uint64_t min_capacity);
  static void InitExtendBuffer(uint8_t* buf,
                               size_t malloc_size,
                               SharedInfo** info_return,
                               uint64_t* capacity_return);

  static void AllocateExtendBuffer(uint64_t min_capacity,
                                   uint8_t** buf_return,
                                   SharedInfo** info_return,
                                   uint64_t* capacity_return);
  static void ReleaseStorage(HeapStorage* storage, uint16_t free_flags);
  static void FreeInternalBuffer(void* buf, void* user_data);

  // 
  IOBuffer* next_{this};
  IOBuffer* prev_{this};

  uint8_t* data_{nullptr};
  uint8_t* buf_{nullptr};
  uint64_t length_{0};
  uint64_t capacity_{0};

  mutable uintptr_t flags_and_shared_info_{0};
  static inline uintptr_t PackFlagsAndSharedInfo(uintptr_t flags,
                                                  SharedInfo* info) {
    uintptr_t uinfo = reinterpret_cast<uintptr_t>(info);
    DCHECK_EQ(flags & ~kFlagsMask, 0);
    DCHECK_EQ(uinfo & kFlagsMask, 0);
    return flags | uinfo;
  }
  // sharedInfo -> GetSharedInfo
  inline SharedInfo* GetSharedInfo() const {
    return reinterpret_cast<SharedInfo*>(flags_and_shared_info_ & ~kFlagsMask); 
  }
  inline void SetSharedInfo(SharedInfo* info) {
    uintptr_t uinfo = reinterpret_cast<uintptr_t>(info);
    DCHECK_EQ(uinfo & kFlagsMask, 0);
    flags_and_shared_info_ = (flags_and_shared_info_ & kFlagsMask) | uinfo;
  }
  inline uintptr_t flags() const {
    return flags_and_shared_info_ & kFlagsMask;
  }
  inline void SetFlags(uintptr_t flags) const {
    DCHECK_EQ(flags & ~kFlagsMask, 0);
    flags_and_shared_info_ |= flags;
  }
  inline void ClearFlags(uintptr_t flags) const {
    DCHECK_EQ(flags & ~kFlagsMask, 0);
    flags_and_shared_info_ &= ~flags;
  }
  inline void SetFlagsAndSharedInfo(uintptr_t flags, SharedInfo* info) {
    flags_and_shared_info_ = PackFlagsAndSharedInfo(flags, info);
  }

  struct DeleterBase {
    virtual ~DeleterBase() {}
    virtual void Dispose(void* p) = 0;
  };
  
  template <class UniquePtr>
  struct UniquePtrDeleter : public DeleterBase {
    typedef typename UniquePtr::pointer Pointer;
    typedef typename UniquePtr::deleter_type Deleter;

    explicit UniquePtrDeleter(Deleter deleter) : deleter_(std::move(deleter)){ }
    void Dispose(void* p) {
      try {
        deleter_(static_cast<Pointer>(p));
        delete this;
      } catch (...) {
        abort();
      }
    }

   private:
    Deleter deleter_;
  };

  static void freeUniquePtrBuffer(void* ptr, void* user_data) {
    static_cast<DeleterBase*>(user_data)->Dispose(ptr);
  }

  DISALLOW_COPY_AND_ASSIGN(IOBuffer);
};

} // namespace fb
#endif // FB_IO_BUFFER_H_
