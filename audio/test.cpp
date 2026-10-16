#include <audio/output.hpp>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

constexpr int kSeekSeconds = 5;

class RawTerminal {
 public:
  RawTerminal() {
    m_ok = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &m_saved) == 0;
    if (m_ok) {
      termios raw = m_saved;
      raw.c_lflag &= ~static_cast<tcflag_t>(ICANON | ECHO);
      tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
  }
  ~RawTerminal() {
    if (m_ok) tcsetattr(STDIN_FILENO, TCSANOW, &m_saved);
  }
  RawTerminal(const RawTerminal&) = delete;
  RawTerminal& operator=(const RawTerminal&) = delete;

 private:
  termios m_saved{};
  bool m_ok = false;
};

enum class Key { None, Space, Left, Right, Quit, Other };

Key read_key(int timeout_ms) {
  pollfd pfd{STDIN_FILENO, POLLIN, 0};
  if (poll(&pfd, 1, timeout_ms) <= 0) return Key::None;

  char buf[8] = {};
  const ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
  if (n <= 0) return Key::Quit;
  if (buf[0] == ' ') return Key::Space;
  if (buf[0] == 'q' || buf[0] == 'Q') return Key::Quit;
  if (n >= 3 && buf[0] == '\x1b' && buf[1] == '[') {
    if (buf[2] == 'D') return Key::Left;
    if (buf[2] == 'C') return Key::Right;
  }
  return Key::Other;
}

class PcmFile {
 public:
  PcmFile(const char* path, std::size_t frame_bytes)
      : m_file(std::fopen(path, "rb")), m_frame_bytes(frame_bytes) {
    if (m_file == nullptr) throw std::runtime_error("cannot open file");
    std::fseek(m_file, 0, SEEK_END);
    m_total_frames =
        static_cast<std::int64_t>(std::ftell(m_file)) / m_frame_bytes;
    std::fseek(m_file, 0, SEEK_SET);
  }
  ~PcmFile() { std::fclose(m_file); }
  PcmFile(const PcmFile&) = delete;
  PcmFile& operator=(const PcmFile&) = delete;

  std::int64_t total_frames() const { return m_total_frames; }

  std::int64_t seek(std::int64_t frame) {
    frame = std::clamp<std::int64_t>(frame, 0, m_total_frames);
    std::fseek(m_file, static_cast<long>(frame * m_frame_bytes), SEEK_SET);
    return frame;
  }

  std::size_t read(void* dst, std::size_t frames) {
    return std::fread(dst, m_frame_bytes, frames, m_file);
  }

 private:
  std::FILE* m_file;
  std::size_t m_frame_bytes;
  std::int64_t m_total_frames = 0;
};

}

int main(int argc, char** argv) try {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s track.pcm [device]\n", argv[0]);
    std::fprintf(stderr,
                 "make one: ffmpeg -i video.mp4 -vn -f s16le -acodec "
                 "pcm_s16le -ar 48000 -ac 2 track.pcm\n");
    return 1;
  }
  const char* device = argc > 2 ? argv[2] : "default";

  audio::Output output(audio::Format{2, 48000},
                       device);
  const audio::Format& format = output.format();
  if (format.channels != 2 || format.sample_rate != 48000) {
    std::fprintf(stderr, "device wants %d ch / %d Hz, file is 2 ch / 48000 Hz;"
                         " try a plughw: device\n",
                 format.channels, format.sample_rate);
    return 1;
  }

  PcmFile file(argv[1], audio::bytes_per_frame(format));
  const double duration = double(file.total_frames()) / format.sample_rate;

  std::atomic<std::int64_t> base_frame{0};
  std::atomic<std::int64_t> seek_target{-1};
  std::atomic<bool> quit{false};
  std::atomic<bool> finished{false};

  std::thread audio_thread([&] {
    std::vector<std::uint8_t> chunk(1024 * audio::bytes_per_frame(format));
    while (!quit) {
      if (const std::int64_t target = seek_target.exchange(-1); target >= 0) {
        output.flush();
        base_frame = file.seek(target);
        finished = false;
      }

      const std::size_t frames = file.read(chunk.data(), 1024);
      if (frames == 0) {
        output.drain();
        finished = true;
        while (!quit && seek_target < 0) {
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        continue;
      }
      output.write(chunk.data(), frames);
    }
  });

  {
    RawTerminal terminal;
    std::printf("Space: pause  Left/Right: seek %d s  q: quit\n",
                kSeekSeconds);

    auto clock_frames = [&] { return base_frame + output.played_frames(); };

    while (!quit) {
      const Key key = read_key(100);
      switch (key) {
        case Key::Space:
          output.pause(!output.paused());
          break;
        case Key::Left:
        case Key::Right: {
          const std::int64_t step =
              std::int64_t{kSeekSeconds} * format.sample_rate;
          const std::int64_t target = std::clamp<std::int64_t>(
              clock_frames() + (key == Key::Left ? -step : step), 0,
              file.total_frames());
          seek_target = target;
          base_frame = target;
          output.flush();
          break;
        }
        case Key::Quit:
          quit = true;
          break;
        default:
          break;
      }
      const double clock = double(clock_frames()) / format.sample_rate;
      std::printf("\r%7.2f / %.2f s %-9s", clock, duration,
                  output.paused() ? "[paused]" : finished ? "[end]" : "");
      std::fflush(stdout);
    }
    std::printf("\n");
  }

  output.flush();
  audio_thread.join();
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "error: %s\n", e.what());
  return 1;
}
