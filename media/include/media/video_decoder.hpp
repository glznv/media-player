#pragma once

#include <limits>
#include <memory>
#include <optional>

#include "demuxer.hpp"
#include "packet.hpp"
#include "video_frame.hpp"

namespace media {

class VideoDecoder final {
 public:
  explicit VideoDecoder(const Demuxer& demuxer);
  ~VideoDecoder();

  VideoDecoder(VideoDecoder&&) noexcept;
  VideoDecoder& operator=(VideoDecoder&&) noexcept;

  void send(const Packet& packet);
  void send_eof();

  std::optional<VideoFrame> receive();

  [[nodiscard]] bool finished() const noexcept;

  void flush(double discard_before = -std::numeric_limits<double>::infinity());

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
