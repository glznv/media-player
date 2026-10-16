#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

namespace player {

struct SeekRequest {
  double target;
  int serial;
};

class Control {
 public:
  [[nodiscard]] int serial() const noexcept { return m_serial.load(); }

  int request_seek(double target) {
    std::lock_guard lock(m_mutex);
    const int serial = m_serial.load() + 1;
    m_serial.store(serial);
    m_seek_target = target;
    m_pending = SeekRequest{target, serial};
    m_cv.notify_all();
    return serial;
  }

  [[nodiscard]] double seek_target() const {
    std::lock_guard lock(m_mutex);
    return m_seek_target;
  }

  [[nodiscard]] SeekRequest current_seek() const {
    std::lock_guard lock(m_mutex);
    return SeekRequest{m_seek_target, m_serial.load()};
  }

  std::optional<SeekRequest> take_seek() {
    std::lock_guard lock(m_mutex);
    return std::exchange(m_pending, std::nullopt);
  }

  void wait_for_seek() {
    std::unique_lock lock(m_mutex);
    m_cv.wait(lock, [&] { return m_pending.has_value() || m_stopped.load(); });
  }

  void stop() {
    std::lock_guard lock(m_mutex);
    m_stopped.store(true);
    m_cv.notify_all();
  }

  [[nodiscard]] bool stopped() const noexcept { return m_stopped.load(); }

  void fail(std::exception_ptr error) {
    {
      std::lock_guard lock(m_mutex);
      if (!m_error) m_error = error;
    }
    stop();
  }

  [[nodiscard]] std::exception_ptr error() const {
    std::lock_guard lock(m_mutex);
    return m_error;
  }

 private:
  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  std::atomic<int> m_serial{0};
  std::atomic<bool> m_stopped{false};
  std::optional<SeekRequest> m_pending;
  double m_seek_target = 0.0;
  std::exception_ptr m_error;
};

}
