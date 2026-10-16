#include "player.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>

namespace player {

namespace {

using SteadyClock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr std::size_t kPacketQueueSize = 256;
constexpr std::size_t kFrameQueueSize = 4;
constexpr double kSeekStep = 5.0;
constexpr auto kMaxSleep = 10ms;
constexpr auto kStatusPeriod = 250ms;
constexpr double kLateTolerance = 0.1;
constexpr auto kMaxDropStreak = 500ms;
constexpr auto kFpsPeriod = 1s;

struct WindowSize {
  int width;
  int height;
};

WindowSize window_size(const media::Demuxer& demuxer) {
  if (!demuxer.video() || demuxer.video()->width <= 0) {
    return {1280, 720};
  }
  const int w = demuxer.video()->width;
  const int h = demuxer.video()->height;
  const double scale = std::min({1.0, 1600.0 / w, 900.0 / h});
  return {std::max(1, static_cast<int>(w * scale)),
          std::max(1, static_cast<int>(h * scale))};
}

std::string file_name(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

render::Frame to_render_frame(const media::VideoFrame& frame) {
  render::Frame out;
  out.width = frame.width();
  out.height = frame.height();
  for (int i = 0; i < 3; ++i) {
    out.planes[i] = frame.plane(i);
    out.strides[i] = frame.stride(i);
  }
  return out;
}

std::string format_time(double seconds) {
  const auto total = static_cast<long>(std::max(0.0, seconds));
  char buf[32];
  if (total >= 3600) {
    std::snprintf(buf, sizeof(buf), "%ld:%02ld:%02ld", total / 3600,
                  total / 60 % 60, total % 60);
  } else {
    std::snprintf(buf, sizeof(buf), "%02ld:%02ld", total / 60, total % 60);
  }
  return buf;
}

}

Player::Player(const Options& options)
    : m_demuxer(options.path),
      m_duration(m_demuxer.duration()),
      m_video_packets(kPacketQueueSize),
      m_audio_packets(kPacketQueueSize),
      m_frames(kFrameQueueSize),
      m_window(m_display, window_size(m_demuxer).width,
               window_size(m_demuxer).height, file_name(options.path)),
      m_context(m_display, m_window) {
  if (m_demuxer.video()) {
    m_video_decoder.emplace(m_demuxer);
  }

  if (m_demuxer.audio()) {
    m_output = std::make_unique<audio::Output>(
        audio::Format{2, m_demuxer.audio()->sample_rate},
        options.audio_device);
    const audio::Format& device = m_output->format();
    m_audio_decoder.emplace(
        m_demuxer, media::AudioFormat{device.channels,
                                      device.sample_rate});
    m_clock = std::make_unique<AudioClock>(*m_output);
  } else {
    m_clock = std::make_unique<SystemClock>();
  }

  m_context.make_current();
  m_context.set_vsync(true);
  m_renderer.emplace(m_context.proc_loader());
}

Player::~Player() { stop_threads(); }

template <typename F>
std::thread Player::spawn(F&& body) {
  return std::thread([this, body = std::forward<F>(body)]() mutable {
    try {
      body();
    } catch (...) {
      m_control.fail(std::current_exception());
    }
  });
}

void Player::start_threads() {
  m_demux_thread = spawn([this] { demux_loop(); });
  if (m_video_decoder) {
    m_video_thread = spawn([this] { video_loop(); });
  }
  if (m_audio_decoder) {
    m_audio_thread = spawn([this] { audio_loop(); });
  }
}

void Player::stop_threads() noexcept {
  m_control.stop();
  m_video_packets.close();
  m_audio_packets.close();
  m_frames.close();
  if (m_output) {
    m_output->pause(false);
    m_output->flush();
  }
  for (std::thread* thread : {&m_demux_thread, &m_video_thread, &m_audio_thread}) {
    if (thread->joinable()) {
      thread->join();
    }
  }
}

void Player::run() {
  m_clock->reset(0.0, 0);
  start_threads();
  try {
    main_loop();
  } catch (...) {
    stop_threads();
    throw;
  }
  stop_threads();
  std::printf("\n");
  if (const std::exception_ptr error = m_control.error()) {
    std::rethrow_exception(error);
  }
}

void Player::demux_loop() {
  const bool has_video = m_video_decoder.has_value();
  const bool has_audio = m_audio_decoder.has_value();

  int serial = 0;
  bool at_end = false;

  auto push_end_of_stream = [&] {
    if (has_video &&
        !m_video_packets.push(PacketItem{std::nullopt, true, serial})) return false;
    if (has_audio &&
        !m_audio_packets.push(PacketItem{std::nullopt, true, serial})) return false;
    return true;
  };

  while (!m_control.stopped()) {
    if (const std::optional<SeekRequest> seek = m_control.take_seek()) {
      if (!m_demuxer.seek(seek->target)) {
        std::fprintf(stderr, "\nseek to %.1f s failed\n", seek->target);
      }
      serial = seek->serial;
      at_end = false;
      m_video_packets.clear();
      m_audio_packets.clear();
      continue;
    }

    if (at_end) {
      m_control.wait_for_seek();
      continue;
    }

    std::optional<media::Packet> packet = m_demuxer.read();
    if (!packet) {
      at_end = true;
      if (!push_end_of_stream()) return;
      continue;
    }

    BoundedQueue<PacketItem>& queue =
        packet->stream() == media::StreamType::Video ? m_video_packets
                                                     : m_audio_packets;
    if (!queue.push(PacketItem{std::move(packet), false, serial})) return;
  }
}

void Player::video_loop() {
  media::VideoDecoder& decoder = *m_video_decoder;
  auto last_pushed = SteadyClock::now();
  int current = 0;

  while (std::optional<PacketItem> item = m_video_packets.pop()) {
    const int serial = item->serial;
    const SeekRequest seek = m_control.current_seek();
    if (serial != seek.serial) {
      continue;
    }
    if (serial != current) {
      decoder.flush(seek.target);
      current = serial;
    }

    const bool end = item->end_of_stream;
    if (end) {
      decoder.send_eof();
    } else {
      decoder.send(*item->packet);
    }

    while (std::optional<media::VideoFrame> frame = decoder.receive()) {
      if (serial != m_control.serial()) break;

      // Drop late frames here, not in the main thread: after a stall (swap_buffers
      // blocks while the window is hidden) it would show the whole backlog.
      const ClockSample clock = m_clock->sample();
      const auto now = SteadyClock::now();
      const bool late = clock.serial == serial &&
                        frame->pts() + frame->duration() <
                            clock.time - kLateTolerance;
      if (late && now - last_pushed < kMaxDropStreak) {
        ++m_dropped;
        continue;
      }
      last_pushed = now;
      if (!m_frames.push(VideoItem{std::move(frame), false, serial})) return;
    }
    if (end && serial == m_control.serial()) {
      if (!m_frames.push(VideoItem{std::nullopt, true, serial})) return;
    }
  }
}

void Player::audio_loop() {
  media::AudioDecoder& decoder = *m_audio_decoder;
  audio::Output& output = *m_output;

  int current = 0;

  while (std::optional<PacketItem> item = m_audio_packets.pop()) {
    const int serial = item->serial;
    const SeekRequest seek = m_control.current_seek();
    if (serial != seek.serial) {
      continue;
    }
    if (serial != current) {
      decoder.flush(seek.target);
      output.flush();
      m_clock->reset(seek.target, serial);
      current = serial;
    }

    const bool end = item->end_of_stream;
    if (end) {
      decoder.send_eof();
    } else {
      decoder.send(*item->packet);
    }

    while (std::optional<media::AudioFrame> chunk = decoder.receive()) {
      if (m_control.stopped() || serial != m_control.serial()) break;
      output.write(chunk->data.data(), chunk->frames);
    }
    if (end && !m_control.stopped() && serial == m_control.serial()) {
      output.drain();
    }
  }
}

double Player::position() const {
  const ClockSample sample = m_clock->sample();
  return sample.serial == m_control.serial() ? sample.time
                                              : m_control.seek_target();
}

void Player::toggle_pause() {
  m_paused = !m_paused;
  m_clock->pause(m_paused);
  std::printf("\n%s at %.2f s\n", m_paused ? "pause" : "resume", position());
  print_status(true);
}

void Player::seek_relative(double delta) {
  const double from = position();
  double target = std::max(0.0, from + delta);
  if (m_duration > 0.0) {
    target = std::min(target, m_duration);
  }

  // Clear the packet queues before request_seek(): clearing after it could
  // erase packets the demux thread has already read for the new position.
  m_video_packets.clear();
  m_audio_packets.clear();
  const int serial = m_control.request_seek(target);
  m_frames.clear();
  if (m_output) {
    m_output->flush();
  } else {
    m_clock->reset(target, serial);
  }
  m_video_ended = false;
  std::printf("\nseek %+.0f s: %.2f -> %.2f s\n", delta, from, target);
  print_status(true);
}

void Player::print_status(bool force) {
  const auto now = SteadyClock::now();
  if (!force && now - m_last_status < kStatusPeriod) {
    return;
  }
  m_last_status = now;
  const char* state = m_paused ? "[paused]" : m_video_ended ? "[end]" : "";
  std::printf("\r  %s / %s  fps %4.1f  dropped %-6ld %-8s",
              format_time(position()).c_str(), format_time(m_duration).c_str(),
              m_fps, m_dropped.load(), state);
  std::fflush(stdout);
}

void Player::main_loop() {
  std::optional<VideoItem> pending;
  bool redraw = true;
  int shown = 0;
  auto fps_since = SteadyClock::now();

  auto next = [&](int serial) -> std::optional<VideoItem> {
    while (std::optional<VideoItem> item = m_frames.try_pop()) {
      if (item->serial == serial) return item;
    }
    return std::nullopt;
  };

  while (!m_window.should_close() && !m_control.stopped()) {
    while (const std::optional<render::Event> event = m_window.poll_event()) {
      switch (*event) {
        case render::Event::Space:  toggle_pause(); break;
        case render::Event::Left:   seek_relative(-kSeekStep); break;
        case render::Event::Right:  seek_relative(+kSeekStep); break;
        case render::Event::Escape:
        case render::Event::Q:      m_window.request_close(); break;
        case render::Event::Redraw: redraw = true; break;
      }
    }

    const int serial = m_control.serial();
    const double now = position();

    if (!pending || pending->serial != serial) {
      pending = next(serial);
    }

    auto wake_at = SteadyClock::now() + kMaxSleep;

    if (pending && pending->end_of_stream) {
      m_video_ended = true;
    } else if (pending) {
      const double pts = pending->frame->pts();
      if (pts <= now) {
        VideoItem show = std::move(*pending);
        pending.reset();
        while (std::optional<VideoItem> candidate = next(serial)) {
          if (!candidate->end_of_stream && candidate->frame->pts() <= now) {
            show = std::move(*candidate);
            ++m_dropped;
            continue;
          }
          pending = std::move(candidate);
          break;
        }
        m_renderer->upload(to_render_frame(*show.frame));
        ++shown;
        redraw = true;
      } else if (!m_paused) {
        const auto until_due = std::chrono::duration_cast<SteadyClock::duration>(
            std::chrono::duration<double>(pts - now));
        wake_at = std::min(wake_at, SteadyClock::now() + until_due);
      }
    }

    if (redraw) {
      m_renderer->render(m_window.width(), m_window.height());
      m_context.swap_buffers();
      redraw = false;
    }

    if (const auto elapsed = SteadyClock::now() - fps_since; elapsed >= kFpsPeriod) {
      m_fps = shown / std::chrono::duration<double>(elapsed).count();
      shown = 0;
      fps_since += elapsed;
    }

    print_status(false);

    std::this_thread::sleep_until(wake_at);
  }
  if (!m_control.stopped()) {
    std::printf("\nquit at %.2f s", position());
  }
}

}
