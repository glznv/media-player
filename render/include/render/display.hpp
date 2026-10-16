#pragma once

#include <memory>

namespace render {

class Display final {
 public:
  Display();
  ~Display();

  Display(Display&&) noexcept;
  Display& operator=(Display&&) noexcept;

  template <typename WindowSystem>
  typename WindowSystem::Display native_handle() const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
