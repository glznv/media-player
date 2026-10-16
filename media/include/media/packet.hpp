#pragma once

#include <memory>

#include "types.hpp"

namespace media {

class Packet final {
 public:
  ~Packet();

  Packet(Packet&&) noexcept;
  Packet& operator=(Packet&&) noexcept;

  [[nodiscard]] StreamType stream() const noexcept;

 private:
  friend struct detail::Access;
  struct Impl;
  explicit Packet(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> m_impl;
};

}
