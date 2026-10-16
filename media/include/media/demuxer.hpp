#pragma once

#include <memory>
#include <optional>
#include <string>

#include "packet.hpp"
#include "types.hpp"

namespace media {

class Demuxer final {
 public:
  explicit Demuxer(const std::string& path);
  ~Demuxer();

  Demuxer(Demuxer&&) noexcept;
  Demuxer& operator=(Demuxer&&) noexcept;

  [[nodiscard]] const std::optional<VideoStreamInfo>& video() const noexcept;
  [[nodiscard]] const std::optional<AudioStreamInfo>& audio() const noexcept;

  [[nodiscard]] double duration() const noexcept;

  std::optional<Packet> read();

  bool seek(double seconds);

 private:
  friend struct detail::Access;
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
