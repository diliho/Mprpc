#pragma once
#include <string>
#include <map>
#include <mutex>
#include <atomic>
#include <sstream>
#include <vector>
#include <functional>
#include <thread>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <cstring>

namespace mprpc {

class MetricsExporter {
public:
    static MetricsExporter& GetInstance() {
        static MetricsExporter instance;
        return instance;
    }
    
    void Start(int port = 9090) {
        m_port = port;
        m_running = true;
        std::thread([this]() { this->ServerLoop(); }).detach();
    }
    
    void Stop() {
        m_running = false;
        if (m_server_fd >= 0) {
            close(m_server_fd);
            m_server_fd = -1;
        }
    }
    
    void IncrCounter(const std::string& name, const std::map<std::string, std::string>& labels = {}, double value = 1.0) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string key = BuildKey(name, labels);
        m_counters[key] += value;
    }
    
    void SetGauge(const std::string& name, const std::map<std::string, std::string>& labels, double value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string key = BuildKey(name, labels);
        m_gauges[key] = value;
    }
    
    void ObserveHistogram(const std::string& name, const std::map<std::string, std::string>& labels, double value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::string key = BuildKey(name, labels);
        m_histograms[key].push_back(value);
    }
    
    std::string RenderMetrics() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostringstream oss;
        
        for (auto& [key, value] : m_counters) {
            oss << key << " " << value << "\n";
        }
        
        for (auto& [key, value] : m_gauges) {
            oss << key << " " << value << "\n";
        }
        
        for (auto& [key, values] : m_histograms) {
            if (!values.empty()) {
                double sum = 0;
                double min_val = values[0];
                double max_val = values[0];
                for (double v : values) {
                    sum += v;
                    if (v < min_val) min_val = v;
                    if (v > max_val) max_val = v;
                }
                double avg = sum / values.size();
                oss << key << "_sum " << sum << "\n";
                oss << key << "_count " << values.size() << "\n";
                oss << key << "_avg " << avg << "\n";
                oss << key << "_min " << min_val << "\n";
                oss << key << "_max " << max_val << "\n";
            }
        }
        
        return oss.str();
    }

private:
    MetricsExporter() : m_server_fd(-1), m_port(9090), m_running(false) {}
    
    std::string BuildKey(const std::string& name, const std::map<std::string, std::string>& labels) {
        std::ostringstream oss;
        oss << name << "{";
        bool first = true;
        for (auto& [k, v] : labels) {
            if (!first) oss << ",";
            oss << k << "=\"" << v << "\"";
            first = false;
        }
        oss << "}";
        return oss.str();
    }
    
    void ServerLoop() {
        m_server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_server_fd < 0) return;
        
        int opt = 1;
        setsockopt(m_server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(m_port);
        
        if (bind(m_server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            close(m_server_fd);
            m_server_fd = -1;
            return;
        }
        
        listen(m_server_fd, 5);
        
        while (m_running) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(m_server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) continue;
            
            HandleClient(client_fd);
            close(client_fd);
        }
    }
    
    void HandleClient(int fd) {
        char buffer[4096] = {0};
        recv(fd, buffer, sizeof(buffer) - 1, 0);
        
        std::string metrics = RenderMetrics();
        
        std::string response = "HTTP/1.1 200 OK\r\n"
                              "Content-Type: text/plain; version=0.0.4\r\n"
                              "Content-Length: " + std::to_string(metrics.size()) + "\r\n"
                              "Connection: close\r\n"
                              "\r\n" + metrics;
        
        send(fd, response.c_str(), response.size(), 0);
    }
    
    int m_server_fd;
    int m_port;
    std::atomic<bool> m_running;
    
    mutable std::mutex m_mutex;
    std::map<std::string, double> m_counters;
    std::map<std::string, double> m_gauges;
    std::map<std::string, std::vector<double>> m_histograms;
};

} // namespace mprpc
