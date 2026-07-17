#pragma once
#include "load_balancer.h"
#include <limits>

namespace mprpc {

class LeastConnectionsBalancer : public LoadBalancer {
public:
    std::string Select(const std::vector<ServiceInstance>& instances,
                       const std::string& service,
                       const std::string& method) override {
        if (instances.empty()) return "";
        
        size_t best_index = 0;
        int min_connections = std::numeric_limits<int>::max();
        
        for (size_t i = 0; i < instances.size(); ++i) {
            int adjusted_connections = instances[i].active_connections * 1000 / 
                                      std::max(1, instances[i].weight);
            
            if (adjusted_connections < min_connections) {
                min_connections = adjusted_connections;
                best_index = i;
            }
        }
        
        return instances[best_index].Address();
    }
    
    std::string Name() const override { return "least_connections"; }
};

} // namespace mprpc
