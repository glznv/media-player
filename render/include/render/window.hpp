#pragma once

#include <memory>
#include <optional>
#include <string>

#include "display.hpp"
#include "event.hpp"

namespace render {
class Window final {
 public:
  Window(const Display& display, int width, int height,
         const std::string& title);
  ~Window();

  Window(Window&&) noexcept;
  Window& operator=(Window&&) noexcept;

  template <typename WindowSystem>
  typename WindowSystem::Window native_handle() const noexcept;

  [[nodiscard]] bool should_close() const noexcept;
  void request_close() noexcept;

  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;

  std::optional<Event> poll_event() noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
