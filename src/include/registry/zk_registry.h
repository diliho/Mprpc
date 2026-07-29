#pragma once
#include "registry.h"
#include "zookeeperutil.h"
#include <memory>

namespace mprpc {

class ZKRegistry : public Registry {
public:
    ZKRegistry(std::shared_ptr<ZKClient> existing_client = nullptr)
        : m_zkclient(existing_client ? existing_client
                                     : std::make_shared<ZKClient>())
    {}

    bool Start(const std::string& host, int port) override {
        (void)host; (void)port;
        return m_zkclient->Start();
    }

    bool IsConnected() const override {
        return m_zkclient->isConnected();
    }

    std::string CreateNode(const std::string& path,
                           const std::string& data = "",
                           int flags = 0) override {
        char actual_path[256] = {0};
        const char* data_ptr = data.empty() ? nullptr : data.c_str();
        int data_len = static_cast<int>(data.size());
        m_zkclient->Create(path.c_str(), data_ptr, data_len,
                           flags, actual_path, sizeof(actual_path));
        if (actual_path[0]) return std::string(actual_path);
        return path;
    }

    bool DeleteNode(const std::string& path) override {
        zhandle_t* zh = m_zkclient->getHandle();
        if (zh && m_zkclient->isConnected()) {
            return zoo_delete(zh, path.c_str(), -1) == ZOK;
        }
        return false;
    }

    std::string GetData(const std::string& path) override {
        return m_zkclient->GetData(path.c_str());
    }

    std::vector<std::string> GetChildren(const std::string& path) override {
        return m_zkclient->GetChildren(path.c_str());
    }

    void WatchService(const std::string& service,
                      std::function<void(const std::vector<ServiceInstance>&)> callback) override {
        (void)service; (void)callback;
    }

    void AddReconnectCallback(std::function<void()> callback) override {
        m_zkclient->addReconnectCallback(std::move(callback));
    }

    zhandle_t* GetHandle() override {
        return m_zkclient->getHandle();
    }

    ZKClient& GetRawClient() {
        return *m_zkclient;
    }

    ZKClient* operator->() { return m_zkclient.get(); }
    const ZKClient* operator->() const { return m_zkclient.get(); }

private:
    std::shared_ptr<ZKClient> m_zkclient;
};

} // namespace mprpc
