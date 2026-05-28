#include "zookeeperutil.h"
#include "mprpcapplication.h"
#include <iostream>
#include "logger.h"
#include "rpcerror.h"

void global_watcher(zhandle_t* zh, int type, int state, const char* path, void* watcherCtx)
{
    if (type != ZOO_SESSION_EVENT) return;
    ZKClient* client = static_cast<ZKClient*>(watcherCtx);
    if (!client) return;

    if (state == ZOO_CONNECTED_STATE)
    {
        LOG_INFO("ZK connected successfully");
        sem_post(client->getConnectSem());
    }
    else if (state == ZOO_CONNECTING_STATE)
    {
        LOG_INFO("ZK connecting...");
    }
    else if (state == ZOO_EXPIRED_SESSION_STATE)
    {
        LOG_ERROR("ZK session expired, reinitializing...");
        client->doReinitialize();
    }
    else if (state == ZOO_AUTH_FAILED_STATE)
    {
        LOG_ERROR("ZK auth failed");
    }
}

ZKClient::ZKClient() : m_zhandle(nullptr), m_connected(false)
{
    sem_init(&m_sem, 0, 0);
}

ZKClient::~ZKClient()
{
    if (m_zhandle != nullptr)
    {
        zookeeper_close(m_zhandle);
    }
    sem_destroy(&m_sem);
}

bool ZKClient::Start()
{
    std::string host = MprpcApplication::GetInstance().GetConfig().Load("zookeeperip");
    std::string port = MprpcApplication::GetInstance().GetConfig().Load("zookeeperport");
    std::string conststr = host + ":" + port;

    if (host.empty() || port.empty())
    {
        LOG_ERROR("ZK config not found (zookeeperip/zookeeperport)");
        return false;
    }

    m_zhandle = zookeeper_init(conststr.c_str(), global_watcher, 3000, nullptr, nullptr, 0);
    if (nullptr == m_zhandle)
    {
        LOG_ERROR("zookeeper_init error");
        return false;
    }

    zoo_set_context(m_zhandle, this);

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 5;
    int ret = sem_timedwait(&m_sem, &ts);
    if (ret == -1)
    {
        LOG_ERROR("ZK connect timeout (5s)");
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
        return false;
    }

    m_connected = true;
    LOG_INFO("zookeeper_init success");
    return true;
}

void ZKClient::doReinitialize()
{
    if (m_zhandle)
    {
        zookeeper_close(m_zhandle);
        m_zhandle = nullptr;
    }
    m_connected = false;
    sem_destroy(&m_sem);
    sem_init(&m_sem, 0, 0);

    LOG_INFO("ZK reconnecting...");
    if (Start())
    {
        LOG_INFO("ZK reconnected");
        if (m_on_reconnect_cb)
        {
            m_on_reconnect_cb();
        }
    }
    else
    {
        LOG_ERROR("ZK reconnection failed, will retry later");
    }
}

void ZKClient::CreateParentNodes(const char* path)
{
    if (!m_zhandle || !m_connected) return;
    std::string parent_path(path);
    size_t last_slash = parent_path.find_last_of('/');
    if (last_slash == 0) return;

    parent_path = parent_path.substr(0, last_slash);
    int flag = zoo_exists(m_zhandle, parent_path.c_str(), 0, nullptr);
    if (flag == ZNONODE)
    {
        CreateParentNodes(parent_path.c_str());
        flag = zoo_create(m_zhandle, parent_path.c_str(), nullptr, 0, &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);
        if (flag == ZOK)
        {
            LOG_INFO("SUCCESS: Create parent node: %s", parent_path.c_str());
        }
        else
        {
            LOG_ERROR("Create parent node failed: %s, error code: %d", parent_path.c_str(), flag);
        }
    }
}

void ZKClient::Create(const char* path, const char* data, int datalen, int state)
{
    if (!m_zhandle || !m_connected)
    {
        LOG_ERROR("ZK not connected, cannot create node: %s", path);
        return;
    }

    CreateParentNodes(path);

    char path_buffer[128];
    int bufferlen = sizeof(path_buffer);
    int flag = zoo_exists(m_zhandle, path, 0, nullptr);
    if (ZNONODE == flag)
    {
        flag = zoo_create(m_zhandle, path, data, datalen, &ZOO_OPEN_ACL_UNSAFE, state, path_buffer, bufferlen);
        if (flag == ZOK)
        {
            std::cout << "SUCCESS: Create znode: " << path << " data: " << (data ? data : "null") << std::endl;
            LOG_INFO("SUCCESS: Create znode: %s", path);
        }
        else
        {
            LOG_ERROR("Create znode failed: %s, error code: %d", path, flag);
        }
    }
    else
    {
        // Node already exists (e.g., ephemeral node from a previous session
        // that hasn't timed out yet). Update its data.
        flag = zoo_set(m_zhandle, path, data, datalen, -1);
        if (flag == ZOK)
        {
            LOG_INFO("Update znode data: %s", path);
        }
        else
        {
            LOG_ERROR("Update znode data failed: %s, error code: %d", path, flag);
        }
    }
}

std::string ZKClient::GetData(const char* path)
{
    if (m_zhandle == nullptr)
    {
        LOG_ERROR("Zookeeper handle is null, cannot get data for path: %s", path);
        return "";
    }

    char buffer[64];
    int bufferlen = sizeof(buffer);
    int flag = zoo_get(m_zhandle, path, 0, buffer, &bufferlen, nullptr);

    if (flag != ZOK)
    {
        LOG_ERROR("Get znode failed, path: %s error code: %d", path, flag);
        return "";
    }
    else
    {
        LOG_INFO("SUCCESS: Get znode data, path: %s", path);
        return buffer;
    }
}
