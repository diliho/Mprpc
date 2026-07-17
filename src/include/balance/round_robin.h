#pragma once
#include "load_balancer.h"
#include <atomic>

namespace mprpc {

class RoundRobinBalancer : public LoadBalancer {
public:
    std::string Select(const std::vector<ServiceInstance>& instances,
                       const std::string& service,
                       const std::string& method) override {
        if (instances.empty()) return "";
        
        size_t index = m_counter.fetch_add(1, std::memory_order_relaxed) % instances.size();
        return instances[index].Address();
    }
    
    std::string Name() const override { return "round_robin"; }

private:
    std::atomic<size_t> m_counter{0};
};

} // namespace mprpc
