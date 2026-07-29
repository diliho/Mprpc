#pragma once
#include <string>
#include <vector>
#include <functional>
#include <zookeeper/zookeeper.h>
#include "balance/load_balancer.h"

namespace mprpc {

class Registry {
public:
    virtual ~Registry() = default;

    virtual bool Start(const std::string& host, int port) = 0;
    virtual bool IsConnected() const = 0;

    // Create a znode. Returns actual path (for sequential nodes).
    // flags: 0 (persistent), ZOO_EPHEMERAL, ZOO_SEQUENCE, or both.
    virtual std::string CreateNode(const std::string& path,
                                   const std::string& data = "",
                                   int flags = 0) = 0;
    virtual bool DeleteNode(const std::string& path) = 0;
    virtual std::string GetData(const std::string& path) = 0;
    virtual std::vector<std::string> GetChildren(const std::string& path) = 0;

    virtual void WatchService(const std::string& service,
                              std::function<void(const std::vector<ServiceInstance>&)> callback) = 0;
    virtual void AddReconnectCallback(std::function<void()> callback) = 0;

    virtual zhandle_t* GetHandle() = 0;
};

} // namespace mprpc
