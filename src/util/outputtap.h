#ifndef OUTPUTTAP_H
#define OUTPUTTAP_H
#include <deque>
#include <mutex>
#include <QtGlobal>

// 每个订阅者独占有界队列，慢消费者不会抢走其他订阅者的数据，也不会阻塞生产者。
// 负载采用共享数据，发布到多个队列时无需复制像素或采样内容。
// 所有方法均受锁保护；takeAll() 转移队列内容，只应由该订阅的消费者调用。
template <class T> class OutputTap
{
public:
    explicit OutputTap(size_t capacity) : m_capacity(capacity)
    {
    }

    void push(const T &value)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(value);
        if (m_queue.size() > m_capacity) {
            m_queue.pop_front();
            ++m_dropped;
        }
    }

    std::deque<T> takeAll()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::deque<T> result;
        result.swap(m_queue);
        return result;
    }

    quint64 dropped() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_dropped;
    }

private:
    const size_t m_capacity;
    mutable std::mutex m_mutex;
    std::deque<T> m_queue;
    quint64 m_dropped = 0;
};

#endif
