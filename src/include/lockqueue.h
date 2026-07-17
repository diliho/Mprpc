#pragma once
#include<mutex>
#include<condition_variable>
#include<queue>
#include<thread>

template<typename T>
class LockQueue
{
public:
    void Push(const T& data)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push(data);
        m_cond.notify_one();
    }

    T Pop()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cond.wait(lock, [this] { return !m_queue.empty() || !m_running; });
        if (!m_running && m_queue.empty())
        {
            return T{};
        }
        T data = m_queue.front();
        m_queue.pop();
        return data;
    }

    void Shutdown()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running = false;
        m_cond.notify_all();
    }

    bool Empty()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    size_t Size()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::queue<T> m_queue;
    bool m_running = true;
};

