#pragma once
#include <semaphore.h>
#include <zookeeper/zookeeper.h>
#include <string>
#include <functional>

class ZKClient
{
public:
    ZKClient();
    ~ZKClient();
    bool Start();
    void Create(const char* path, const char* data, int datalen, int state=0);
    std::string GetData(const char* path);
    zhandle_t* getHandle() { return m_zhandle; }
    bool isConnected() const { return m_connected; }
    sem_t* getConnectSem() { return &m_sem; }

    using ReconnectCallback = std::function<void()>;
    void setOnReconnectCallback(ReconnectCallback cb) { m_on_reconnect_cb = std::move(cb); }
    void doReinitialize();

private:
    zhandle_t* m_zhandle;
    sem_t m_sem;
    bool m_connected;
    ReconnectCallback m_on_reconnect_cb;

    void CreateParentNodes(const char* path);
};
