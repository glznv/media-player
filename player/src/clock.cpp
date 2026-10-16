#include "clock.hpp"

namespace player {

AudioClock::AudioClock(audio::Output& output)
    : m_output(output), m_rate(output.format().sample_rate) {}

ClockSample AudioClock::sample() const {
  std::lock_guard lock(m_mutex);
  return {m_base + static_cast<double>(m_output.played_frames()) / m_rate,
          m_serial};
}

void AudioClock::reset(double time, int serial) {
  std::lock_guard lock(m_mutex);
  m_base = time;
  m_serial = serial;
}

void AudioClock::pause(bool paused) {
  m_output.pause(paused);
}

SystemClock::SystemClock() : m_started(Steady::now()) {}

double SystemClock::elapsed_locked() const {
  if (m_paused) {
    return 0.0;
  }
  return std::chrono::duration<double>(Steady::now() - m_started).count();
}

ClockSample SystemClock::sample() const {
  std::lock_guard lock(m_mutex);
  return {m_base + elapsed_locked(), m_serial};
}

void SystemClock::reset(double time, int serial) {
  std::lock_guard lock(m_mutex);
  m_base = time;
  m_started = Steady::now();
  m_serial = serial;
}

void SystemClock::pause(bool paused) {
  std::lock_guard lock(m_mutex);
  if (paused == m_paused) {
    return;
  }
  if (paused) {
    m_base += elapsed_locked();
  } else {
    m_started = Steady::now();
  }
  m_paused = paused;
}

}
