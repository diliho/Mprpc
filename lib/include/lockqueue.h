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
    std::lock_guard<std::mutex>lock(m_mutex);
    m_queue.push(data);
    m_cond.notify_one();

 }
 T Pop()
{    
    
    std::unique_lock<std::mutex>lock(m_mutex);
    while(m_queue.empty())
    {
        //等待条件变量
        m_cond.wait(lock);
    }
    T data=m_queue.front();
    m_queue.pop();
    return data;

}
private:
 std:: mutex  m_mutex;
 std::condition_variable m_cond;
 std::queue<T> m_queue;
};

