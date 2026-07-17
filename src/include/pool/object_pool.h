#pragma once
#include <memory>
#include <vector>
#include <mutex>
#include <functional>
#include <unordered_set>

namespace mprpc {

template<typename T>
class ObjectPool {
public:
    explicit ObjectPool(size_t initial_size = 256) {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (size_t i = 0; i < initial_size; ++i) {
            m_free_list.push_back(new T());
        }
    }

    ~ObjectPool() {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto* obj : m_free_list) {
            delete obj;
        }
        for (auto* obj : m_in_use) {
            delete obj;
        }
    }

    std::unique_ptr<T, std::function<void(T*)>> Acquire() {
        T* obj = nullptr;
        
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (!m_free_list.empty()) {
                obj = m_free_list.back();
                m_free_list.pop_back();
                m_in_use.insert(obj);
            }
        }
        
        if (!obj) {
            obj = new T();
            std::lock_guard<std::mutex> lock(m_mutex);
            m_in_use.insert(obj);
        }
        
        return std::unique_ptr<T, std::function<void(T*)>>(
            obj,
            [this](T* ptr) { this->Release(ptr); }
        );
    }

    void Release(T* obj) {
        if (!obj) return;
        
        std::lock_guard<std::mutex> lock(m_mutex);
        m_in_use.erase(obj);
        m_free_list.push_back(obj);
    }

    size_t FreeCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_free_list.size();
    }

    size_t InUseCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_in_use.size();
    }

private:
    std::vector<T*> m_free_list;
    std::unordered_set<T*> m_in_use;
    mutable std::mutex m_mutex;
};

} // namespace mprpc
