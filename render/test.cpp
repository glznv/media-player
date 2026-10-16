#include <render/display.hpp>
#include <render/gl_context.hpp>
#include <render/gl_renderer.hpp>
#include <render/window.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

const char* key_name(render::Event event) {
  switch (event) {
    case render::Event::Space:  return "Space";
    case render::Event::Left:   return "Left";
    case render::Event::Right:  return "Right";
    case render::Event::Escape: return "Escape";
    case render::Event::Q:      return "Q";
    case render::Event::Redraw: break;
  }
  return nullptr;
}

}

int main(int argc, char** argv) try {
  int width = 0;
  int height = 0;
  char tail = 0;
  if (argc != 3 || std::sscanf(argv[1], "%dx%d%c", &width, &height, &tail) != 2 ||
      width <= 0 || height <= 0) {
    std::fprintf(stderr,
                 "usage: %s WIDTHxHEIGHT frame.yuv\n"
                 "make a frame: ffmpeg -i video.mp4 -ss 10 -frames:v 1 "
                 "-f rawvideo -pix_fmt yuv420p frame.yuv\n",
                 argv[0]);
    return 1;
  }

  const int cw = (width + 1) / 2;
  const int ch = (height + 1) / 2;
  const std::size_t y_size = static_cast<std::size_t>(width) * height;
  const std::size_t c_size = static_cast<std::size_t>(cw) * ch;

  std::ifstream in(argv[2], std::ios::binary);
  if (!in) {
    throw std::runtime_error(std::string("cannot open ") + argv[2]);
  }
  const std::vector<std::uint8_t> data{std::istreambuf_iterator<char>(in), {}};
  if (data.size() != y_size + 2 * c_size) {
    throw std::runtime_error(std::to_string(data.size()) +
                             " bytes, a yuv420p frame of this size is " +
                             std::to_string(y_size + 2 * c_size));
  }

  render::Frame frame;
  frame.width = width;
  frame.height = height;
  frame.planes = {data.data(), data.data() + y_size, data.data() + y_size + c_size};
  frame.strides = {width, cw, cw};

  const double scale = std::min({1.0, 1600.0 / width, 900.0 / height});
  render::Display display;
  render::Window window(display, std::max(1, static_cast<int>(width * scale)),
                        std::max(1, static_cast<int>(height * scale)),
                        "test_render");
  render::GLContext context(display, window);
  context.make_current();
  context.set_vsync(true);
  render::GLRenderer renderer(context.proc_loader());
  renderer.upload(frame);

  bool redraw = true;
  while (!window.should_close()) {
    while (const std::optional<render::Event> event = window.poll_event()) {
      if (const char* key = key_name(*event)) {
        std::fprintf(stderr, "key: %s\n", key);
      }
      if (*event == render::Event::Escape || *event == render::Event::Q) {
        window.request_close();
      } else if (*event == render::Event::Redraw) {
        redraw = true;
      }
    }

    if (redraw) {
      renderer.render(window.width(), window.height());
      context.swap_buffers();
      redraw = false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "error: %s\n", e.what());
  return 1;
}
