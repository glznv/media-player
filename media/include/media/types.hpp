#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace media {

namespace detail {
struct Access;
}

enum class StreamType { Video, Audio };

struct AudioFormat {
  int channels = 2;
  int sample_rate = 48000;
};

std::size_t bytes_per_frame(const AudioFormat& format) noexcept;

struct VideoStreamInfo {
  std::string codec;
  int width = 0;
  int height = 0;
  double fps = 0.0;
};

struct AudioStreamInfo {
  std::string codec;
  int channels = 0;
  int sample_rate = 0;
};

struct AudioFrame {
  AudioFormat format;
  std::vector<std::uint8_t> data;
  std::size_t frames = 0;
  double pts = 0.0;
};

}
