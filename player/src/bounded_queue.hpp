#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

namespace player {

template <typename T>
class BoundedQueue {
 public:
  explicit BoundedQueue(std::size_t capacity) : m_capacity(capacity) {}

  BoundedQueue(const BoundedQueue&) = delete;
  BoundedQueue& operator=(const BoundedQueue&) = delete;

  bool push(T item) {
    std::unique_lock lock(m_mutex);
    m_not_full.wait(lock,
                    [&] { return m_closed || m_items.size() < m_capacity; });
    if (m_closed) {
      return false;
    }
    m_items.push_back(std::move(item));
    m_not_empty.notify_one();
    return true;
  }

  std::optional<T> pop() {
    std::unique_lock lock(m_mutex);
    m_not_empty.wait(lock, [&] { return m_closed || !m_items.empty(); });
    if (m_closed) {
      return std::nullopt;
    }
    return take_front();
  }

  std::optional<T> try_pop() {
    std::lock_guard lock(m_mutex);
    if (m_closed || m_items.empty()) {
      return std::nullopt;
    }
    return take_front();
  }

  void clear() {
    std::lock_guard lock(m_mutex);
    m_items.clear();
    m_not_full.notify_all();
  }

  void close() {
    std::lock_guard lock(m_mutex);
    m_closed = true;
    m_items.clear();
    m_not_full.notify_all();
    m_not_empty.notify_all();
  }

 private:
  T take_front() {
    T item = std::move(m_items.front());
    m_items.pop_front();
    m_not_full.notify_one();
    return item;
  }

  const std::size_t m_capacity;
  std::mutex m_mutex;
  std::condition_variable m_not_empty;
  std::condition_variable m_not_full;
  std::deque<T> m_items;
  bool m_closed = false;
};

}
