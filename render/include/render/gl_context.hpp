#pragma once

#include <memory>

#include "display.hpp"
#include "gl_loader.hpp"
#include "window.hpp"

namespace render {

class GLContext final {
 public:
  GLContext(const Display& display, const Window& window);
  ~GLContext();

  GLContext(GLContext&&) noexcept;
  GLContext& operator=(GLContext&&) noexcept;

  void make_current() const;
  void release_current() const noexcept;

  void swap_buffers() const noexcept;

  bool set_vsync(bool enabled) const noexcept;

  [[nodiscard]] GLProcLoader proc_loader() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
