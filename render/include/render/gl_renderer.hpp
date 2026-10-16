#pragma once

#include <memory>

#include "frame.hpp"
#include "gl_loader.hpp"

namespace render {

class GLRenderer final {
 public:
  explicit GLRenderer(GLProcLoader loader);
  ~GLRenderer();

  GLRenderer(GLRenderer&&) noexcept;
  GLRenderer& operator=(GLRenderer&&) noexcept;

  void upload(const Frame& frame);

  void render(int viewport_width, int viewport_height) const noexcept;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
