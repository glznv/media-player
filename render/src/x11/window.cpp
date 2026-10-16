#include <render/platform/x11.hpp>
#include <render/window.hpp>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <stdexcept>

namespace render {

namespace {

std::optional<Event> translate_key(KeySym sym) noexcept {
  switch (sym) {
    case XK_space:  return Event::Space;
    case XK_Left:   return Event::Left;
    case XK_Right:  return Event::Right;
    case XK_Escape: return Event::Escape;
    case XK_q:      return Event::Q;
    default:        return std::nullopt;
  }
}

}

struct Window::Impl {
  ::Display* m_display = nullptr;
  ::Window m_window = 0;
  Atom m_wm_delete = 0;
  int m_width = 0;
  int m_height = 0;
  bool m_should_close = false;

  Impl(::Display* display, int width, int height, const std::string& title)
      : m_display(display), m_width(width), m_height(height) {
    const int screen = DefaultScreen(display);

    XSetWindowAttributes attrs{};
    attrs.background_pixel = BlackPixel(display, screen);
    attrs.event_mask = KeyPressMask | StructureNotifyMask | ExposureMask;

    m_window = XCreateWindow(
        display, RootWindow(display, screen), 0, 0,
        static_cast<unsigned>(width), static_cast<unsigned>(height), 0,
        CopyFromParent, InputOutput, CopyFromParent,
        CWBackPixel | CWEventMask, &attrs);
    if (m_window == 0) {
      throw std::runtime_error("XCreateWindow failed");
    }

    XStoreName(display, m_window, title.c_str());

    m_wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, m_window, &m_wm_delete, 1);

    XMapWindow(display, m_window);
    XFlush(display);
  }

  ~Impl() {
    XDestroyWindow(m_display, m_window);
    XFlush(m_display);
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  std::optional<Event> translate(XEvent& event) noexcept {
    switch (event.type) {
      case KeyPress:
        return translate_key(XLookupKeysym(&event.xkey, 0));
      case ConfigureNotify:
        m_width = event.xconfigure.width;
        m_height = event.xconfigure.height;
        return Event::Redraw;
      case Expose:
        return Event::Redraw;
      case ClientMessage:
        if (static_cast<Atom>(event.xclient.data.l[0]) == m_wm_delete) {
          m_should_close = true;
        }
        return std::nullopt;
      default:
        return std::nullopt;
    }
  }
};

Window::Window(const Display& display, int width, int height,
               const std::string& title)
    : m_impl(std::make_unique<Impl>(display.native_handle<X11WindowSystem>(),
                                    width, height, title)) {}

Window::~Window() = default;
Window::Window(Window&&) noexcept = default;
Window& Window::operator=(Window&&) noexcept = default;

template <>
X11WindowSystem::Window Window::native_handle<X11WindowSystem>()
    const noexcept {
  return m_impl->m_window;
}

bool Window::should_close() const noexcept { return m_impl->m_should_close; }
void Window::request_close() noexcept { m_impl->m_should_close = true; }

int Window::width() const noexcept { return m_impl->m_width; }
int Window::height() const noexcept { return m_impl->m_height; }

std::optional<Event> Window::poll_event() noexcept {
  while (XPending(m_impl->m_display) > 0) {
    XEvent event{};
    XNextEvent(m_impl->m_display, &event);
    if (const std::optional<Event> translated = m_impl->translate(event)) {
      return translated;
    }
  }
  return std::nullopt;
}

}
