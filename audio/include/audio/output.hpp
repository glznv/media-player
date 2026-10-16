#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace audio {

struct Format {
  int channels = 2;
  int sample_rate = 48000;
};

std::size_t bytes_per_frame(const Format& format) noexcept;

class Output final {
 public:
  explicit Output(const Format& format,
                  const std::string& device = "default");
  ~Output();

  Output(Output&&) noexcept;
  Output& operator=(Output&&) noexcept;

  [[nodiscard]] const Format& format() const noexcept;

  std::size_t write(const void* data, std::size_t frames);

  void drain();

  void pause(bool paused);
  [[nodiscard]] bool paused() const noexcept;

  void flush();

  [[nodiscard]] std::int64_t played_frames() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

}
