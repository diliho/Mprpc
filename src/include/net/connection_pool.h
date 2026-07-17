#pragma once
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
#include <atomic>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstring>

namespace mprpc {

struct ConnectionInfo {
    std::string host;
    uint16_t port;
    int fd = -1;
    bool in_use = false;
};

class ConnectionPool {
public:
    ConnectionPool(const std::string& host, uint16_t port, 
                   size_t max_size = 16, int timeout_ms = 3000)
        : m_host(host), m_port(port), m_max_size(max_size), m_timeout_ms(timeout_ms) {}

    ~ConnectionPool() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& conn : m_connections) {
            if (conn.fd >= 0) {
                close(conn.fd);
            }
        }
    }

    int Acquire() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        for (auto& conn : m_connections) {
            if (!conn.in_use && conn.fd >= 0) {
                conn.in_use = true;
                return conn.fd;
            }
        }
        
        if (m_connections.size() < m_max_size) {
            int fd = CreateConnection();
            if (fd >= 0) {
                ConnectionInfo info;
                info.host = m_host;
                info.port = m_port;
                info.fd = fd;
                info.in_use = true;
                m_connections.push_back(info);
                return fd;
            }
        }
        
        return -1;
    }

    void Release(int fd) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& conn : m_connections) {
            if (conn.fd == fd) {
                conn.in_use = false;
                return;
            }
        }
        close(fd);
    }

    void Remove(int fd) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_connections.begin(); it != m_connections.end(); ++it) {
            if (it->fd == fd) {
                if (it->fd >= 0) close(it->fd);
                m_connections.erase(it);
                return;
            }
        }
    }

    size_t Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_connections.size();
    }

    size_t AvailableCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        size_t count = 0;
        for (const auto& conn : m_connections) {
            if (!conn.in_use && conn.fd >= 0) count++;
        }
        return count;
    }

private:
    int CreateConnection() {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(m_port);
        addr.sin_addr.s_addr = inet_addr(m_host.c_str());
        
        struct timeval tv;
        tv.tv_sec = m_timeout_ms / 1000;
        tv.tv_usec = (m_timeout_ms % 1000) * 1000;
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
        
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(fd);
            return -1;
        }
        
        return fd;
    }

    std::string m_host;
    uint16_t m_port;
    size_t m_max_size;
    int m_timeout_ms;
    
    mutable std::mutex m_mutex;
    std::vector<ConnectionInfo> m_connections;
};

} // namespace mprpc
