#include <iostream>
#include <cassert>
#include <map>
#include <vector>
#include "balance/load_balancer_factory.h"

using namespace mprpc;

void TestRoundRobin() {
    std::cout << "Testing RoundRobin..." << std::endl;
    
    auto balancer = LoadBalancerFactory::Create("round_robin");
    assert(balancer->Name() == "round_robin");
    
    std::vector<ServiceInstance> instances = {
        {"127.0.0.1", 8001, 1, 0},
        {"127.0.0.1", 8002, 1, 0},
        {"127.0.0.1", 8003, 1, 0}
    };
    
    std::map<std::string, int> counts;
    for (int i = 0; i < 300; ++i) {
        std::string addr = balancer->Select(instances, "test", "method");
        counts[addr]++;
    }
    
    assert(counts.size() == 3);
    for (auto& [addr, count] : counts) {
        assert(count == 100);
    }
    
    std::cout << "RoundRobin: PASSED" << std::endl;
}

void TestWeightedRoundRobin() {
    std::cout << "Testing WeightedRoundRobin..." << std::endl;
    
    auto balancer = LoadBalancerFactory::Create("weighted_round_robin");
    assert(balancer->Name() == "weighted_round_robin");
    
    std::vector<ServiceInstance> instances = {
        {"127.0.0.1", 8001, 1, 0},
        {"127.0.0.1", 8002, 2, 0},
        {"127.0.0.1", 8003, 3, 0}
    };
    
    std::map<std::string, int> counts;
    for (int i = 0; i < 600; ++i) {
        std::string addr = balancer->Select(instances, "test", "method");
        counts[addr]++;
    }
    
    assert(counts.size() == 3);
    assert(counts["127.0.0.1:8001"] == 100);
    assert(counts["127.0.0.1:8002"] == 200);
    assert(counts["127.0.0.1:8003"] == 300);
    
    std::cout << "WeightedRoundRobin: PASSED" << std::endl;
}

void TestConsistentHash() {
    std::cout << "Testing ConsistentHash..." << std::endl;
    
    auto balancer = LoadBalancerFactory::Create("consistent_hash");
    assert(balancer->Name() == "consistent_hash");
    
    std::vector<ServiceInstance> instances = {
        {"127.0.0.1", 8001, 1, 0},
        {"127.0.0.1", 8002, 1, 0},
        {"127.0.0.1", 8003, 1, 0}
    };
    
    std::map<std::string, int> counts;
    for (int i = 0; i < 300; ++i) {
        std::string addr = balancer->Select(instances, "test", "method" + std::to_string(i));
        counts[addr]++;
    }
    
    assert(counts.size() == 3);
    for (auto& [addr, count] : counts) {
        assert(count > 0);
    }
    
    std::cout << "ConsistentHash: PASSED" << std::endl;
}

void TestLeastConnections() {
    std::cout << "Testing LeastConnections..." << std::endl;
    
    auto balancer = LoadBalancerFactory::Create("least_connections");
    assert(balancer->Name() == "least_connections");
    
    std::vector<ServiceInstance> instances = {
        {"127.0.0.1", 8001, 1, 10},
        {"127.0.0.1", 8002, 1, 5},
        {"127.0.0.1", 8003, 1, 1}
    };
    
    for (int i = 0; i < 10; ++i) {
        std::string addr = balancer->Select(instances, "test", "method");
        assert(addr == "127.0.0.1:8003");
    }
    
    std::cout << "LeastConnections: PASSED" << std::endl;
}

int main() {
    TestRoundRobin();
    TestWeightedRoundRobin();
    TestConsistentHash();
    TestLeastConnections();
    
    std::cout << "\nAll load balancer tests PASSED!" << std::endl;
    return 0;
}
