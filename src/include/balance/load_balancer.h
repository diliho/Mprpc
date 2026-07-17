#pragma once
#include <string>
#include <vector>
#include <memory>

namespace mprpc {

struct ServiceInstance {
    std::string host;
    uint16_t port;
    int weight = 1;
    int active_connections = 0;
    std::string zone;
    std::string version;
    
    std::string Address() const { return host + ":" + std::to_string(port); }
};

class LoadBalancer {
public:
    virtual ~LoadBalancer() = default;
    virtual std::string Select(const std::vector<ServiceInstance>& instances,
                               const std::string& service,
                               const std::string& method) = 0;
    virtual std::string Name() const = 0;
};

} // namespace mprpc
