#ifndef OUTPUTTAP_H
#define OUTPUTTAP_H
#include <deque>
#include <mutex>
#include <QtGlobal>

// Each subscriber owns a bounded queue. Payloads are implicitly shared; a slow
// subscriber cannot consume another subscriber's data or stall the producer.
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
