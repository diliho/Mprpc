#pragma once
#include <vector>
#include <chrono>
#include <mutex>
#include <atomic>
#include <cstddef>

namespace mprpc {

class SlidingWindowCircuitBreaker {
public:
    enum class State { CLOSED, OPEN, HALF_OPEN };
    
    SlidingWindowCircuitBreaker(int window_size = 20, 
                                int failure_threshold = 5,
                                int recovery_timeout_sec = 30,
                                int half_open_max_probes = 3)
        : m_window_size(window_size),
          m_failure_threshold(failure_threshold),
          m_recovery_timeout_sec(recovery_timeout_sec),
          m_half_open_max_probes(half_open_max_probes) {}
    
    bool AllowRequest() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        switch (m_state) {
            case State::CLOSED:
                return true;
                
            case State::OPEN: {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - m_last_failure_time).count();
                if (elapsed >= m_recovery_timeout_sec) {
                    m_state = State::HALF_OPEN;
                    m_half_open_probes = 0;
                    return true;
                }
                return false;
            }
            
            case State::HALF_OPEN:
                return m_half_open_probes < m_half_open_max_probes;
        }
        return true;
    }
    
    void RecordSuccess() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_results.push_back(true);
        if (m_results.size() > m_window_size) {
            m_results.erase(m_results.begin());
        }
        
        if (m_state == State::HALF_OPEN) {
            m_half_open_probes++;
            if (m_half_open_probes >= m_half_open_max_probes) {
                m_state = State::CLOSED;
                m_results.clear();
            }
        }
        
        if (m_state == State::CLOSED) {
            m_consecutive_failures = 0;
        }
    }
    
    void RecordFailure() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        m_results.push_back(false);
        if (m_results.size() > m_window_size) {
            m_results.erase(m_results.begin());
        }
        
        m_last_failure_time = std::chrono::steady_clock::now();
        
        if (m_state == State::HALF_OPEN) {
            m_state = State::OPEN;
            return;
        }
        
        m_consecutive_failures++;
        if (m_consecutive_failures >= m_failure_threshold) {
            m_state = State::OPEN;
        }
    }
    
    State GetState() const { return m_state; }
    double GetFailureRate() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_results.empty()) return 0.0;
        int failures = 0;
        for (bool success : m_results) {
            if (!success) failures++;
        }
        return static_cast<double>(failures) / m_results.size();
    }
    
    void Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = State::CLOSED;
        m_consecutive_failures = 0;
        m_results.clear();
    }

private:
    int m_window_size;
    int m_failure_threshold;
    int m_recovery_timeout_sec;
    int m_half_open_max_probes;
    
    mutable std::mutex m_mutex;
    State m_state = State::CLOSED;
    int m_consecutive_failures = 0;
    int m_half_open_probes = 0;
    std::vector<bool> m_results;
    std::chrono::steady_clock::time_point m_last_failure_time;
};

} // namespace mprpc
