#include <render/gl_context.hpp>
#include <render/platform/x11.hpp>

#include <GL/glx.h>
#include <X11/Xlib.h>

#include <cstring>
#include <stdexcept>

namespace render {

namespace {

using CreateContextAttribsARB = ::GLXContext (*)(::Display*, GLXFBConfig,
                                                 ::GLXContext, Bool,
                                                 const int*);
using SwapIntervalEXT = void (*)(::Display*, GLXDrawable, int);
using SwapIntervalMESA = int (*)(unsigned);

GLProc get_proc(const char* name) noexcept {
  return glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name));
}

bool has_extension(::Display* display, int screen, const char* name) {
  const char* list = glXQueryExtensionsString(display, screen);
  const std::size_t len = std::strlen(name);
  for (const char* p = list; p != nullptr && (p = std::strstr(p, name));
       p += len) {
    const bool starts = p == list || p[-1] == ' ';
    const bool ends = p[len] == ' ' || p[len] == '\0';
    if (starts && ends) {
      return true;
    }
  }
  return false;
}

GLXFBConfig choose_config(::Display* display, int screen, ::Window window) {
  XWindowAttributes window_attrs{};
  if (XGetWindowAttributes(display, window, &window_attrs) == 0) {
    throw std::runtime_error("XGetWindowAttributes failed");
  }
  const VisualID visual_id = XVisualIDFromVisual(window_attrs.visual);

  const int attribs[] = {
      GLX_X_RENDERABLE,  True,
      GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
      GLX_RENDER_TYPE,   GLX_RGBA_BIT,
      GLX_DOUBLEBUFFER,  True,
      GLX_RED_SIZE,      8,
      GLX_GREEN_SIZE,    8,
      GLX_BLUE_SIZE,     8,
      None,
  };

  int count = 0;
  GLXFBConfig* configs = glXChooseFBConfig(display, screen, attribs, &count);
  GLXFBConfig result = nullptr;
  for (int i = 0; i < count && result == nullptr; ++i) {
    int id = 0;
    glXGetFBConfigAttrib(display, configs[i], GLX_VISUAL_ID, &id);
    if (static_cast<VisualID>(id) == visual_id) {
      result = configs[i];
    }
  }
  if (configs != nullptr) {
    XFree(configs);
  }
  if (result == nullptr) {
    throw std::runtime_error("no GLXFBConfig matches the window visual");
  }
  return result;
}

// A failed glXCreateContextAttribsARB raises an X error, and the default
// Xlib handler terminates the process.
bool g_x_error = false;
int record_x_error(::Display*, XErrorEvent*) {
  g_x_error = true;
  return 0;
}

}

struct GLContext::Impl {
  ::Display* m_display = nullptr;
  ::Window m_window = 0;
  ::GLXContext m_context = nullptr;

  Impl(::Display* display, ::Window window)
      : m_display(display), m_window(window) {
    const int screen = DefaultScreen(display);

    if (!has_extension(display, screen, "GLX_ARB_create_context_profile")) {
      throw std::runtime_error("GLX_ARB_create_context_profile unsupported");
    }
    const auto create_context = reinterpret_cast<CreateContextAttribsARB>(
        get_proc("glXCreateContextAttribsARB"));

    const GLXFBConfig config = choose_config(display, screen, window);

    const int context_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None,
    };

    g_x_error = false;
    const auto old_handler = XSetErrorHandler(record_x_error);
    m_context = create_context(display, config, nullptr, True,
                               context_attribs);
    XSync(display, False);
    XSetErrorHandler(old_handler);

    if (m_context == nullptr || g_x_error) {
      if (m_context != nullptr) {
        glXDestroyContext(display, m_context);
      }
      throw std::runtime_error("failed to create OpenGL 3.3 core context");
    }
  }

  ~Impl() {
    if (glXGetCurrentContext() == m_context) {
      glXMakeCurrent(m_display, None, nullptr);
    }
    glXDestroyContext(m_display, m_context);
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
};

GLContext::GLContext(const Display& display, const Window& window)
    : m_impl(std::make_unique<Impl>(
          display.native_handle<X11WindowSystem>(),
          window.native_handle<X11WindowSystem>())) {}

GLContext::~GLContext() = default;
GLContext::GLContext(GLContext&&) noexcept = default;
GLContext& GLContext::operator=(GLContext&&) noexcept = default;

void GLContext::make_current() const {
  if (glXMakeCurrent(m_impl->m_display, m_impl->m_window,
                     m_impl->m_context) == False) {
    throw std::runtime_error("glXMakeCurrent failed");
  }
}

void GLContext::release_current() const noexcept {
  glXMakeCurrent(m_impl->m_display, None, nullptr);
}

void GLContext::swap_buffers() const noexcept {
  glXSwapBuffers(m_impl->m_display, m_impl->m_window);
}

bool GLContext::set_vsync(bool enabled) const noexcept {
  const int screen = DefaultScreen(m_impl->m_display);
  const int interval = enabled ? 1 : 0;

  if (has_extension(m_impl->m_display, screen, "GLX_EXT_swap_control")) {
    const auto fn = reinterpret_cast<SwapIntervalEXT>(
        get_proc("glXSwapIntervalEXT"));
    fn(m_impl->m_display, m_impl->m_window, interval);
    return true;
  }
  if (has_extension(m_impl->m_display, screen, "GLX_MESA_swap_control")) {
    const auto fn = reinterpret_cast<SwapIntervalMESA>(
        get_proc("glXSwapIntervalMESA"));
    return fn(static_cast<unsigned>(interval)) == 0;
  }
  return false;
}

GLProcLoader GLContext::proc_loader() const noexcept { return &get_proc; }

}
