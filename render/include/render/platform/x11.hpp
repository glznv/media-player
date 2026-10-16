#pragma once

#include "../display.hpp"
#include "../window.hpp"

struct _XDisplay;

namespace render {

struct X11WindowSystem {
  using Display = ::_XDisplay*;
  using Window = unsigned long;
};

template <>
X11WindowSystem::Display Display::native_handle<X11WindowSystem>()
    const noexcept;

template <>
X11WindowSystem::Window Window::native_handle<X11WindowSystem>()
    const noexcept;

}
