#include <audio/output.hpp>

#include <alsa/asoundlib.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace audio {

std::size_t bytes_per_frame(const Format& format) noexcept {
  return 2 * static_cast<std::size_t>(format.channels);
}

namespace {

void check(int err, const char* what) {
  if (err < 0) {
    throw std::runtime_error(std::string("ALSA ") + what + ": " +
                             snd_strerror(err));
  }
}

struct HwParamsDeleter {
  void operator()(snd_pcm_hw_params_t* p) const noexcept {
    snd_pcm_hw_params_free(p);
  }
};
struct SwParamsDeleter {
  void operator()(snd_pcm_sw_params_t* p) const noexcept {
    snd_pcm_sw_params_free(p);
  }
};
struct PcmDeleter {
  void operator()(snd_pcm_t* p) const noexcept { snd_pcm_close(p); }
};

constexpr unsigned kBufferTimeUs = 100'000;
constexpr unsigned kPeriodTimeUs = 25'000;

}

struct Output::Impl {
  std::unique_ptr<snd_pcm_t, PcmDeleter> m_pcm;
  Format m_format;
  std::size_t m_frame_bytes = 0;
  snd_pcm_uframes_t m_buffer_frames = 0;
  snd_pcm_uframes_t m_period_frames = 0;
  bool m_can_pause = false;

  mutable std::mutex m_mutex;
  std::condition_variable m_cv;
  bool m_paused = false;
  std::int64_t m_written = 0;
  std::uint64_t m_generation = 0;

  Impl(const Format& format, const std::string& device) : m_format(format) {
    snd_pcm_t* pcm = nullptr;
    // Non-blocking, and waiting happens on m_cv with m_mutex released: this is
    // what lets pause()/flush() from another thread wake write().
    const int err = snd_pcm_open(&pcm, device.c_str(),
                                 SND_PCM_STREAM_PLAYBACK, SND_PCM_NONBLOCK);
    if (err < 0) {
      throw std::runtime_error(
          "cannot open audio device \"" + device + "\": " + snd_strerror(err) +
          ". Pick another one from `aplay -L` (e.g. plughw:1,0).");
    }
    m_pcm.reset(pcm);
    configure();
  }

  void configure() {
    snd_pcm_t* pcm = m_pcm.get();

    snd_pcm_hw_params_t* raw_hw = nullptr;
    check(snd_pcm_hw_params_malloc(&raw_hw), "hw_params_malloc");
    std::unique_ptr<snd_pcm_hw_params_t, HwParamsDeleter> hw(raw_hw);

    check(snd_pcm_hw_params_any(pcm, raw_hw), "hw_params_any");
    check(snd_pcm_hw_params_set_access(pcm, raw_hw,
                                       SND_PCM_ACCESS_RW_INTERLEAVED),
          "set_access");
    check(snd_pcm_hw_params_set_format(pcm, raw_hw, SND_PCM_FORMAT_S16_LE),
          "set_format");

    auto channels = static_cast<unsigned>(m_format.channels);
    check(snd_pcm_hw_params_set_channels_near(pcm, raw_hw, &channels),
          "set_channels_near");
    auto rate = static_cast<unsigned>(m_format.sample_rate);
    check(snd_pcm_hw_params_set_rate_near(pcm, raw_hw, &rate, nullptr),
          "set_rate_near");

    unsigned buffer_us = kBufferTimeUs;
    unsigned period_us = kPeriodTimeUs;
    check(snd_pcm_hw_params_set_buffer_time_near(pcm, raw_hw, &buffer_us,
                                                 nullptr),
          "set_buffer_time_near");
    check(snd_pcm_hw_params_set_period_time_near(pcm, raw_hw, &period_us,
                                                 nullptr),
          "set_period_time_near");
    check(snd_pcm_hw_params(pcm, raw_hw), "hw_params");

    snd_pcm_hw_params_get_buffer_size(raw_hw, &m_buffer_frames);
    snd_pcm_hw_params_get_period_size(raw_hw, &m_period_frames, nullptr);
    m_can_pause = snd_pcm_hw_params_can_pause(raw_hw) != 0;

    m_format.channels = static_cast<int>(channels);
    m_format.sample_rate = static_cast<int>(rate);
    m_frame_bytes = bytes_per_frame(m_format);

    snd_pcm_sw_params_t* raw_sw = nullptr;
    check(snd_pcm_sw_params_malloc(&raw_sw), "sw_params_malloc");
    std::unique_ptr<snd_pcm_sw_params_t, SwParamsDeleter> sw(raw_sw);
    check(snd_pcm_sw_params_current(pcm, raw_sw), "sw_params_current");
    check(snd_pcm_sw_params_set_start_threshold(pcm, raw_sw, m_buffer_frames),
          "set_start_threshold");
    check(snd_pcm_sw_params_set_avail_min(pcm, raw_sw, m_period_frames),
          "set_avail_min");
    check(snd_pcm_sw_params(pcm, raw_sw), "sw_params");
  }

  void ensure_writable() {
    snd_pcm_t* pcm = m_pcm.get();
    switch (snd_pcm_state(pcm)) {
      case SND_PCM_STATE_SETUP:
      case SND_PCM_STATE_XRUN:
        check(snd_pcm_prepare(pcm), "prepare");
        break;
      case SND_PCM_STATE_SUSPENDED:
        if (snd_pcm_resume(pcm) < 0) {
          check(snd_pcm_prepare(pcm), "prepare");
        }
        break;
      default:
        break;
    }
  }

  std::int64_t played_locked() const {
    snd_pcm_sframes_t delay = 0;
    switch (snd_pcm_state(m_pcm.get())) {
      case SND_PCM_STATE_PREPARED:
      case SND_PCM_STATE_RUNNING:
      case SND_PCM_STATE_PAUSED:
      case SND_PCM_STATE_DRAINING:
        if (snd_pcm_delay(m_pcm.get(), &delay) < 0) delay = 0;
        break;
      default:
        break;
    }
    return std::max<std::int64_t>(0, m_written - delay);
  }

  std::chrono::microseconds frames_to_time(std::int64_t frames) const {
    return std::chrono::microseconds(frames * 1'000'000 /
                                     m_format.sample_rate);
  }

  std::size_t write(const void* data, std::size_t frames) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::size_t done = 0;

    std::unique_lock lock(m_mutex);
    const std::uint64_t generation = m_generation;

    while (done < frames) {
      if (m_generation != generation) {
        break;
      }
      if (m_paused) {
        m_cv.wait(lock);
        continue;
      }

      ensure_writable();

      snd_pcm_sframes_t avail = snd_pcm_avail_update(m_pcm.get());
      if (avail < 0) {
        check(snd_pcm_recover(m_pcm.get(), static_cast<int>(avail), 1),
              "recover");
        continue;
      }

      // Before the start fill whatever fits: playback starts only with a full
      // buffer, so waiting for a whole chunk here would never start it.
      const bool running = snd_pcm_state(m_pcm.get()) == SND_PCM_STATE_RUNNING;
      const auto wanted = static_cast<snd_pcm_sframes_t>(
          running ? std::min<std::size_t>(frames - done, m_period_frames) : 1);
      if (avail < wanted) {
        const auto wait = frames_to_time(wanted - avail);
        m_cv.wait_for(lock,
                      std::max(wait, std::chrono::microseconds(1000)));
        continue;
      }

      const auto chunk = std::min<snd_pcm_sframes_t>(
          avail, static_cast<snd_pcm_sframes_t>(frames - done));
      const snd_pcm_sframes_t n =
          snd_pcm_writei(m_pcm.get(), bytes + done * m_frame_bytes,
                         static_cast<snd_pcm_uframes_t>(chunk));
      if (n == -EAGAIN) {
        continue;
      }
      if (n < 0) {
        check(snd_pcm_recover(m_pcm.get(), static_cast<int>(n), 1),
              "recover");
        continue;
      }
      done += static_cast<std::size_t>(n);
      m_written += n;
    }
    return done;
  }

  void drain() {
    std::unique_lock lock(m_mutex);
    const std::uint64_t generation = m_generation;

    while (m_generation == generation) {
      if (m_paused) {
        m_cv.wait(lock);
        continue;
      }
      const snd_pcm_state_t state = snd_pcm_state(m_pcm.get());
      if (state == SND_PCM_STATE_PREPARED && m_written > 0) {
        check(snd_pcm_start(m_pcm.get()), "start");
        continue;
      }
      if (state != SND_PCM_STATE_RUNNING) {
        break;
      }
      const std::int64_t left = m_written - played_locked();
      if (left <= 0) {
        break;
      }
      m_cv.wait_for(lock, std::max(frames_to_time(left),
                                   std::chrono::microseconds(1000)));
    }
  }

  void pause(bool paused) {
    std::lock_guard lock(m_mutex);
    if (paused == m_paused) {
      return;
    }
    m_paused = paused;

    snd_pcm_t* pcm = m_pcm.get();
    const snd_pcm_state_t state = snd_pcm_state(pcm);

    if (paused) {
      if (state == SND_PCM_STATE_RUNNING) {
        if (!m_can_pause || snd_pcm_pause(pcm, 1) < 0) {
          m_written = played_locked();
          snd_pcm_drop(pcm);
        }
      }
    } else {
      if (state == SND_PCM_STATE_PAUSED) {
        check(snd_pcm_pause(pcm, 0), "pause");
      }
    }
    m_cv.notify_all();
  }

  void flush() {
    std::lock_guard lock(m_mutex);
    snd_pcm_drop(m_pcm.get());
    snd_pcm_prepare(m_pcm.get());
    m_written = 0;
    ++m_generation;
    m_cv.notify_all();
  }
};

Output::Output(const Format& format, const std::string& device)
    : m_impl(std::make_unique<Impl>(format, device)) {}

Output::~Output() = default;
Output::Output(Output&&) noexcept = default;
Output& Output::operator=(Output&&) noexcept = default;

const Format& Output::format() const noexcept { return m_impl->m_format; }

std::size_t Output::write(const void* data, std::size_t frames) {
  return m_impl->write(data, frames);
}

void Output::drain() { m_impl->drain(); }

void Output::pause(bool paused) { m_impl->pause(paused); }

bool Output::paused() const noexcept {
  std::lock_guard lock(m_impl->m_mutex);
  return m_impl->m_paused;
}

void Output::flush() { m_impl->flush(); }

std::int64_t Output::played_frames() const {
  std::lock_guard lock(m_impl->m_mutex);
  return m_impl->played_locked();
}

}
