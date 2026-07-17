#pragma once
#include "registry.h"
#include "zookeeperutil.h"
#include <memory>
#include <sstream>

namespace mprpc {

class ZKRegistry : public Registry {
public:
    ZKRegistry() : m_zkclient(std::make_unique<ZKClient>()) {}
    
    bool Start(const std::string& host, int port) override {
        return m_zkclient->Start();
    }
    
    bool IsConnected() const override {
        return m_zkclient->isConnected();
    }
    
    bool RegisterService(const std::string& service,
                         const std::string& address,
                         const ServiceInstance& meta = {}) override {
        std::string service_path = "/" + service;
        m_zkclient->Create(service_path.c_str(), nullptr, 0);
        
        m_zkclient->Create(service_path.c_str(), address.c_str(), 
                          address.size(), ZOO_EPHEMERAL);
        return true;
    }
    
    bool UnregisterService(const std::string& service,
                           const std::string& address) override {
        std::string service_path = "/" + service;
        zhandle_t* zh = m_zkclient->getHandle();
        if (zh && m_zkclient->isConnected()) {
            zoo_delete(zh, service_path.c_str(), -1);
            return true;
        }
        return false;
    }
    
    std::vector<ServiceInstance> DiscoverService(
        const std::string& service) override {
        std::vector<ServiceInstance> instances;
        std::string path = "/" + service;
        std::string data = m_zkclient->GetData(path.c_str());
        
        if (!data.empty()) {
            ServiceInstance instance;
            size_t colon = data.find(':');
            if (colon != std::string::npos) {
                instance.host = data.substr(0, colon);
                instance.port = std::stoi(data.substr(colon + 1));
                instances.push_back(instance);
            }
        }
        
        return instances;
    }
    
    void WatchService(const std::string& service,
                      std::function<void(const std::vector<ServiceInstance>&)> callback) override {
        // Watch functionality is handled by MprpcChannel's existing watcher
    }
    
    std::string GetData(const std::string& path) override {
        return m_zkclient->GetData(path.c_str());
    }
    
    bool CreateNode(const std::string& path, 
                   const std::string& data = "",
                   bool ephemeral = true) override {
        int state = ephemeral ? ZOO_EPHEMERAL : 0;
        m_zkclient->Create(path.c_str(), data.c_str(), data.size(), state);
        return true;
    }
    
    bool DeleteNode(const std::string& path) override {
        zhandle_t* zh = m_zkclient->getHandle();
        if (zh && m_zkclient->isConnected()) {
            return zoo_delete(zh, path.c_str(), -1) == ZOK;
        }
        return false;
    }
    
    void AddReconnectCallback(std::function<void()> callback) override {
        m_zkclient->addReconnectCallback(callback);
    }
    
    ZKClient& GetZKClient() { return *m_zkclient; }

private:
    std::unique_ptr<ZKClient> m_zkclient;
};

} // namespace mprpc
