#pragma once
#include "load_balancer.h"
#include <map>
#include <functional>
#include <algorithm>

namespace mprpc {

class ConsistentHashBalancer : public LoadBalancer {
public:
    explicit ConsistentHashBalancer(int virtual_nodes = 150) 
        : m_virtual_nodes(virtual_nodes) {}
    
    std::string Select(const std::vector<ServiceInstance>& instances,
                       const std::string& service,
                       const std::string& method) override {
        if (instances.empty()) return "";
        
        if (m_ring.size() != instances.size() * m_virtual_nodes) {
            BuildRing(instances);
        }
        
        std::string key = service + "/" + method;
        size_t hash = std::hash<std::string>{}(key);
        
        auto it = m_ring.lower_bound(hash);
        if (it == m_ring.end()) {
            it = m_ring.begin();
        }
        
        return it->second;
    }
    
    std::string Name() const override { return "consistent_hash"; }

private:
    void BuildRing(const std::vector<ServiceInstance>& instances) {
        m_ring.clear();
        for (const auto& instance : instances) {
            std::string addr = instance.Address();
            for (int i = 0; i < m_virtual_nodes; ++i) {
                std::string virtual_key = addr + "#" + std::to_string(i);
                size_t hash = std::hash<std::string>{}(virtual_key);
                m_ring[hash] = addr;
            }
        }
    }

    int m_virtual_nodes;
    std::map<size_t, std::string> m_ring;
};

} // namespace mprpc
