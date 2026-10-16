#include <render/display.hpp>
#include <render/platform/x11.hpp>

#include <stdexcept>

#include <X11/Xlib.h>

namespace render {

namespace {
struct X11DisplayDeleter {
  void operator()(::Display* ptr) const noexcept { XCloseDisplay(ptr); }
};
}

struct Display::Impl {
  std::unique_ptr<::Display, X11DisplayDeleter> m_display;
};

Display::Display() : m_impl(std::make_unique<Impl>()) {
  ::Display* display = XOpenDisplay(nullptr);
  if (display == nullptr) {
    throw std::runtime_error("XOpenDisplay failed");
  }
  m_impl->m_display.reset(display);
}

Display::~Display() = default;
Display::Display(Display&&) noexcept = default;
Display& Display::operator=(Display&&) noexcept = default;

template <>
X11WindowSystem::Display Display::native_handle<X11WindowSystem>()
    const noexcept {
  return m_impl->m_display.get();
}

}
