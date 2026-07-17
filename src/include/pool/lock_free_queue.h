#pragma once
#include <atomic>
#include <array>
#include <optional>

namespace mprpc {

template<typename T, size_t Capacity = 1024>
class LockFreeQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
public:
    bool Push(const T& item) {
        size_t head = m_head.load(std::memory_order_relaxed);
        size_t next_head = (head + 1) & (Capacity - 1);
        
        if (next_head == m_tail.load(std::memory_order_acquire)) {
            return false;  // Queue full
        }
        
        m_buffer[head] = item;
        m_head.store(next_head, std::memory_order_release);
        return true;
    }
    
    std::optional<T> Pop() {
        size_t tail = m_tail.load(std::memory_order_relaxed);
        
        if (tail == m_head.load(std::memory_order_acquire)) {
            return std::nullopt;  // Queue empty
        }
        
        T item = m_buffer[tail];
        m_tail.store((tail + 1) & (Capacity - 1), std::memory_order_release);
        return item;
    }
    
    bool Empty() const {
        return m_head.load(std::memory_order_acquire) == 
               m_tail.load(std::memory_order_acquire);
    }
    
    size_t Size() const {
        size_t head = m_head.load(std::memory_order_acquire);
        size_t tail = m_tail.load(std::memory_order_acquire);
        return (head - tail + Capacity) & (Capacity - 1);
    }

private:
    std::array<T, Capacity> m_buffer;
    alignas(64) std::atomic<size_t> m_head{0};
    alignas(64) std::atomic<size_t> m_tail{0};
};

} // namespace mprpc
