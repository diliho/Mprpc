#pragma once
#include "load_balancer.h"
#include <vector>
#include <algorithm>

namespace mprpc {

class WeightedRoundRobinBalancer : public LoadBalancer {
public:
    std::string Select(const std::vector<ServiceInstance>& instances,
                       const std::string& service,
                       const std::string& method) override {
        if (instances.empty()) return "";
        
        if (m_current_weights.size() != instances.size()) {
            m_current_weights.resize(instances.size(), 0);
        }
        
        int total_weight = 0;
        for (size_t i = 0; i < instances.size(); ++i) {
            total_weight += instances[i].weight;
            m_current_weights[i] += instances[i].weight;
        }
        
        size_t best_index = 0;
        int best_weight = m_current_weights[0];
        
        for (size_t i = 1; i < instances.size(); ++i) {
            if (m_current_weights[i] > best_weight) {
                best_weight = m_current_weights[i];
                best_index = i;
            }
        }
        
        m_current_weights[best_index] -= total_weight;
        
        return instances[best_index].Address();
    }
    
    std::string Name() const override { return "weighted_round_robin"; }

private:
    std::vector<int> m_current_weights;
};

} // namespace mprpc
