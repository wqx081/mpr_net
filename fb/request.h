#ifndef FB_REQUEST_H_
#define FB_REQUEST_H_
#include "fb/rw_spin_lock.h"

#include <map>
#include <memory>
#include <thread>
#include <glog/logging.h>

namespace fb {

class RequestData {
 public:
  virtual ~RequestData() {}
};

class RequestContext;
extern RequestContext* g_default_context;

class RequestContext {
 public:

  static void Create() {
    GetStaticContext() = std::make_shared<RequestContext>();  
  }

  static RequestContext* get() {
    if (GetStaticContext() == nullptr) {
      if (g_default_context == nullptr) {
        g_default_context = new RequestContext;
      }
      return g_default_context;
    }
    return GetStaticContext().get();
  }

  void SetContextData(const std::string& val,
                      std::unique_ptr<RequestData> data) {
    RWSpinLock::WriteHolder guard(lock_);
    if (data_.find(val) != data_.end()) {
      LOG_FIRST_N(WARNING, 1) << "Called RequestContext::SetContextData with data already set";
      data_[val] = nullptr;
    } else {
      data_[val] = std::move(data);
    }
  }

  bool HasContextData(const std::string& val) {
    RWSpinLock::ReadHolder guard(lock_);
    return data_.find(val) != data_.end();
  }
  
  RequestData* GetContextData(const std::string& val) {
    RWSpinLock::ReadHolder guard(lock_);
    auto r = data_.find(val);
    if (r == data_.end()) {
      return nullptr;
    } else {
      return r->second.get();
    }
  }
  
  void ClearContextData(const std::string& val) {
    RWSpinLock::WriteHolder guard(lock_);
    data_.erase(val);
  }

  static std::shared_ptr<RequestContext> SetContext(std::shared_ptr<RequestContext> ctx) {
    std::shared_ptr<RequestContext> old_ctx;
    if (GetStaticContext()) {
      old_ctx = GetStaticContext(); 
    }
    GetStaticContext() = ctx;
    return old_ctx;
  }

  static std::shared_ptr<RequestContext> SaveContext() {
    return GetStaticContext();
  }

  static std::shared_ptr<RequestContext>& GetStaticContext() {
    //TODO
    static thread_local std::shared_ptr<RequestContext> context;
    return context;
  }

 private:
  RWSpinLock lock_;
  std::map<std::string, std::unique_ptr<RequestData>> data_;
};

} // namespace fb
#endif // FB_REQUEST_H_
