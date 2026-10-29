#ifndef OUTPUTTAP_H
#define OUTPUTTAP_H
#include <deque>
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>
#include <QtGlobal>

// Each subscriber owns a bounded queue. Payloads are implicitly shared; a slow
// subscriber cannot consume another subscriber's data or stall the producer.
template<class T> class OutputTap {
public:
    explicit OutputTap(size_t capacity) : m_capacity(capacity) {}
    void push(const T &value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(value);
        if (m_queue.size()>m_capacity) { m_queue.pop_front(); ++m_dropped; }
    }
    std::deque<T> take() {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::deque<T> result; result.swap(m_queue); return result;
    }
    quint64 dropped() const { std::lock_guard<std::mutex> lock(m_mutex); return m_dropped; }
private:
    const size_t m_capacity;
    mutable std::mutex m_mutex;
    std::deque<T> m_queue;
    quint64 m_dropped = 0;
};

template<class T> class OutputBus {
public:
    // Caller holds the producer's mutex for subscribe/publish/active.
    std::shared_ptr<OutputTap<T>> subscribe(size_t capacity) {
        auto tap=std::make_shared<OutputTap<T>>(capacity); m_taps.push_back(tap); return tap;
    }
    bool active() { prune(); return !m_taps.empty(); }
    void publish(const T &value) {
        for (auto it=m_taps.begin(); it!=m_taps.end();) {
            if (auto tap=it->lock()) { tap->push(value); ++it; } else it=m_taps.erase(it);
        }
    }
private:
    void prune() { m_taps.erase(std::remove_if(m_taps.begin(),m_taps.end(),[](const auto &tap) { return tap.expired(); }),m_taps.end()); }
    std::vector<std::weak_ptr<OutputTap<T>>> m_taps;
};
#endif
