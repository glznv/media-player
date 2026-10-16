#pragma once

#include <audio/output.hpp>
#include <media/audio_decoder.hpp>
#include <media/demuxer.hpp>
#include <media/video_decoder.hpp>
#include <render/display.hpp>
#include <render/gl_context.hpp>
#include <render/gl_renderer.hpp>
#include <render/window.hpp>

#include <atomic>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "bounded_queue.hpp"
#include "clock.hpp"
#include "control.hpp"

namespace player {

struct Options {
  std::string path;
  std::string audio_device = "default";
};

struct PacketItem {
  std::optional<media::Packet> packet;
  bool end_of_stream = false;
  int serial = 0;
};

struct VideoItem {
  std::optional<media::VideoFrame> frame;
  bool end_of_stream = false;
  int serial = 0;
};

class Player final {
 public:
  explicit Player(const Options& options);
  ~Player();

  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  void run();

 private:
  void demux_loop();
  void video_loop();
  void audio_loop();
  void main_loop();

  void start_threads();
  void stop_threads() noexcept;

  void toggle_pause();
  void seek_relative(double delta);
  [[nodiscard]] double position() const;
  void print_status(bool force);

  template <typename F>
  std::thread spawn(F&& body);

  media::Demuxer m_demuxer;
  std::optional<media::VideoDecoder> m_video_decoder;
  std::optional<media::AudioDecoder> m_audio_decoder;
  std::unique_ptr<audio::Output> m_output;
  std::unique_ptr<Clock> m_clock;
  const double m_duration;

  Control m_control;
  BoundedQueue<PacketItem> m_video_packets;
  BoundedQueue<PacketItem> m_audio_packets;
  BoundedQueue<VideoItem> m_frames;

  render::Display m_display;
  render::Window m_window;
  render::GLContext m_context;
  std::optional<render::GLRenderer> m_renderer;
  bool m_paused = false;
  bool m_video_ended = false;
  std::atomic<long> m_dropped{0};
  std::chrono::steady_clock::time_point m_last_status{};

  std::thread m_demux_thread;
  std::thread m_video_thread;
  std::thread m_audio_thread;
};

}
