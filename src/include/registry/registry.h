#pragma once
#include <string>
#include <vector>
#include <functional>
#include "balance/load_balancer.h"

namespace mprpc {

class Registry {
public:
    virtual ~Registry() = default;
    
    virtual bool Start(const std::string& host, int port) = 0;
    virtual bool IsConnected() const = 0;
    
    virtual bool RegisterService(const std::string& service,
                                 const std::string& address,
                                 const ServiceInstance& meta = {}) = 0;
    virtual bool UnregisterService(const std::string& service,
                                   const std::string& address) = 0;
    
    virtual std::vector<ServiceInstance> DiscoverService(
        const std::string& service) = 0;
    
    virtual void WatchService(const std::string& service,
                              std::function<void(const std::vector<ServiceInstance>&)> callback) = 0;
    
    virtual std::string GetData(const std::string& path) = 0;
    virtual bool CreateNode(const std::string& path, 
                           const std::string& data = "",
                           bool ephemeral = true) = 0;
    virtual bool DeleteNode(const std::string& path) = 0;
    
    virtual void AddReconnectCallback(std::function<void()> callback) = 0;
};

} // namespace mprpc
