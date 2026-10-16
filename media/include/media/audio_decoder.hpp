#pragma once

#include <limits>
#include <memory>
#include <optional>

#include "demuxer.hpp"
#include "packet.hpp"
#include "types.hpp"

namespace media {

class AudioDecoder final {
 public:
  AudioDecoder(const Demuxer& demuxer, const AudioFormat& output);
  ~AudioDecoder();

  AudioDecoder(AudioDecoder&&) noexcept;
  AudioDecoder& operator=(AudioDecoder&&) noexcept;

  [[nodiscard]] const AudioFormat& format() const noexcept;

  void send(const Packet& packet);
  void send_eof();

  std::optional<AudioFrame> receive();

  [[nodiscard]] bool finished() const noexcept;

  void flush(double discard_before = -std::numeric_limits<double>::infinity());

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
