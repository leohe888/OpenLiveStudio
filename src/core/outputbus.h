#ifndef OUTPUTBUS_H
#define OUTPUTBUS_H
#include "core/outputtap.h"
#include <memory>
#include <vector>
#include <algorithm>

template <class T> class OutputBus
{
public:
    // 订阅、发布和查询订阅者时，调用方必须持有生产者的互斥锁；总线本身不加锁。
    std::shared_ptr<OutputTap<T>> subscribe(size_t capacity)
    {
        auto tap = std::make_shared<OutputTap<T>>(capacity);
        m_taps.push_back(tap);
        return tap;
    }

    bool hasSubscribers()
    {
        removeExpiredSubscribers();
        return !m_taps.empty();
    }

    void publish(const T &value)
    {
        for (auto it = m_taps.begin(); it != m_taps.end();) {
            if (auto tap = it->lock()) {
                tap->push(value);
                ++it;
            } else
                it = m_taps.erase(it);
        }
    }

private:
    void removeExpiredSubscribers()
    {
        m_taps.erase(std::remove_if(m_taps.begin(), m_taps.end(),
                                    [](const auto &tap) {
                                        return tap.expired();
                                    }),
                     m_taps.end());
    }

    std::vector<std::weak_ptr<OutputTap<T>>> m_taps;
};
#endif
