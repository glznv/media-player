#pragma once

#include <array>
#include <cstdint>

namespace render {

struct Frame {
  int width = 0;
  int height = 0;
  std::array<const std::uint8_t*, 3> planes{};
  std::array<int, 3> strides{};
};

}
