#include "net/network_monitor.h"
#include "net/logging.h"

namespace {
const uint32_t UPDATE_NETWORKS_MESSAGE = 1;

// This is set by NetworkMonitorFactory::SetFactory and the caller of
// NetworkMonitorFactory::SetFactory must be responsible for calling
// ReleaseFactory to destroy the factory.
net::NetworkMonitorFactory* network_monitor_factory = nullptr;
}  // namespace

namespace net {
NetworkMonitorInterface::NetworkMonitorInterface() {}

NetworkMonitorInterface::~NetworkMonitorInterface() {}

NetworkMonitorBase::NetworkMonitorBase() : worker_thread_(Thread::Current()) {}
NetworkMonitorBase::~NetworkMonitorBase() {}

void NetworkMonitorBase::OnNetworksChanged() {
  LOG(INFO) << "Network change is received at the network monitor";
  worker_thread_->Post(FROM_HERE, this, UPDATE_NETWORKS_MESSAGE);
}

void NetworkMonitorBase::OnMessage(Message* msg) {
  (void)msg;
  assert(msg->message_id == UPDATE_NETWORKS_MESSAGE);
  SignalNetworksChanged();
}

NetworkMonitorFactory::NetworkMonitorFactory() {}
NetworkMonitorFactory::~NetworkMonitorFactory() {}

void NetworkMonitorFactory::SetFactory(NetworkMonitorFactory* factory) {
  if (network_monitor_factory != nullptr) {
    delete network_monitor_factory;
  }
  network_monitor_factory = factory;
}

void NetworkMonitorFactory::ReleaseFactory(NetworkMonitorFactory* factory) {
  if (factory == network_monitor_factory) {
    SetFactory(nullptr);
  }
}

NetworkMonitorFactory* NetworkMonitorFactory::GetFactory() {
  return network_monitor_factory;
}

}  // namespace rtc
