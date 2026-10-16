#pragma once

#include <audio/output.hpp>

#include <chrono>
#include <mutex>

namespace player {

struct ClockSample {
  double time = 0.0;
  int serial = 0;
};

class Clock {
 public:
  virtual ~Clock() = default;
  [[nodiscard]] virtual ClockSample sample() const = 0;
  virtual void reset(double time, int serial) = 0;
  virtual void pause(bool paused) = 0;
};

class AudioClock final : public Clock {
 public:
  explicit AudioClock(audio::Output& output);

  [[nodiscard]] ClockSample sample() const override;
  void reset(double time, int serial) override;
  void pause(bool paused) override;

 private:
  audio::Output& m_output;
  const double m_rate;
  mutable std::mutex m_mutex;
  double m_base = 0.0;
  int m_serial = 0;
};

class SystemClock final : public Clock {
 public:
  SystemClock();

  [[nodiscard]] ClockSample sample() const override;
  void reset(double time, int serial) override;
  void pause(bool paused) override;

 private:
  using Steady = std::chrono::steady_clock;
  double elapsed_locked() const;

  mutable std::mutex m_mutex;
  double m_base = 0.0;
  Steady::time_point m_started;
  bool m_paused = false;
  int m_serial = 0;
};

}
