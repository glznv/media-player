#pragma once

#include <cstdint>
#include <memory>

#include "types.hpp"

namespace media {

class VideoFrame final {
 public:
  ~VideoFrame();

  VideoFrame(VideoFrame&&) noexcept;
  VideoFrame& operator=(VideoFrame&&) noexcept;

  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;

  [[nodiscard]] const std::uint8_t* plane(int index) const noexcept;
  [[nodiscard]] int stride(int index) const noexcept;

  [[nodiscard]] double pts() const noexcept;
  [[nodiscard]] double duration() const noexcept;

 private:
  friend struct detail::Access;
  struct Impl;
  explicit VideoFrame(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> m_impl;
};

}
