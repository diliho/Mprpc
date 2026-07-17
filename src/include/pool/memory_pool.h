#pragma once
#include <vector>
#include <memory>
#include <mutex>
#include <cstddef>
#include <new>

namespace mprpc {

class MemoryPool {
public:
    explicit MemoryPool(size_t block_size, size_t blocks_per_chunk = 256)
        : m_block_size(block_size), m_blocks_per_chunk(blocks_per_chunk) {}

    ~MemoryPool() {
        for (auto& chunk : m_chunks) {
            ::operator delete(chunk);
        }
    }

    void* Allocate() {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (m_free_list) {
            void* ptr = m_free_list;
            m_free_list = *static_cast<void**>(m_free_list);
            m_allocated_count++;
            return ptr;
        }
        
        if (!m_current_chunk || m_current_pos >= m_blocks_per_chunk) {
            Grow();
        }
        
        void* ptr = static_cast<char*>(m_current_chunk) + m_current_pos * m_block_size;
        m_current_pos++;
        m_allocated_count++;
        return ptr;
    }

    void Deallocate(void* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(m_mutex);
        *static_cast<void**>(ptr) = m_free_list;
        m_free_list = ptr;
        m_allocated_count--;
    }

    size_t GetAllocatedCount() const { return m_allocated_count; }
    size_t GetBlockSize() const { return m_block_size; }

private:
    void Grow() {
        void* chunk = ::operator new(m_block_size * m_blocks_per_chunk);
        m_chunks.push_back(chunk);
        m_current_chunk = chunk;
        m_current_pos = 0;
    }

    size_t m_block_size;
    size_t m_blocks_per_chunk;
    
    std::vector<void*> m_chunks;
    void* m_current_chunk = nullptr;
    size_t m_current_pos = 0;
    
    void* m_free_list = nullptr;
    size_t m_allocated_count = 0;
    
    std::mutex m_mutex;
};

} // namespace mprpc
