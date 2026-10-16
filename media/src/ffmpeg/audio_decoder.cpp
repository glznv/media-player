#include <media/audio_decoder.hpp>

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "internal.hpp"

namespace media {

namespace {

struct FrameDeleter {
  void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};

struct SwrDeleter {
  void operator()(SwrContext* ctx) const noexcept { swr_free(&ctx); }
};

}

struct AudioDecoder::Impl {
  detail::DecoderCore m_core;
  AudioFormat m_format;
  std::size_t m_frame_bytes;
  AVChannelLayout m_out_layout{};
  std::unique_ptr<AVFrame, FrameDeleter> m_frame{av_frame_alloc()};

  std::unique_ptr<SwrContext, SwrDeleter> m_swr;
  AVChannelLayout m_in_layout{};
  int m_in_format = -1;
  int m_in_rate = 0;

  double m_discard_before = -std::numeric_limits<double>::infinity();
  double m_next_pts = 0.0;
  bool m_tail_done = false;

  Impl(const detail::StreamParams& stream, const AudioFormat& output)
      : m_core(stream, StreamType::Audio),
        m_format(output),
        m_frame_bytes(bytes_per_frame(output)) {
    if (!m_frame) {
      throw std::bad_alloc();
    }
    if (output.channels <= 0 || output.sample_rate <= 0) {
      throw std::invalid_argument("invalid output audio format");
    }
    av_channel_layout_default(&m_out_layout, output.channels);
  }

  ~Impl() {
    av_channel_layout_uninit(&m_out_layout);
    av_channel_layout_uninit(&m_in_layout);
  }

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  void ensure_resampler(const AVFrame* frame) {
    if (m_swr && frame->format == m_in_format &&
        frame->sample_rate == m_in_rate &&
        av_channel_layout_compare(&frame->ch_layout, &m_in_layout) == 0) {
      return;
    }

    SwrContext* raw = nullptr;
    int err = swr_alloc_set_opts2(&raw, &m_out_layout, AV_SAMPLE_FMT_S16,
                                  m_format.sample_rate, &frame->ch_layout,
                                  static_cast<AVSampleFormat>(frame->format),
                                  frame->sample_rate, 0, nullptr);
    m_swr.reset(raw);
    if (err >= 0) {
      err = swr_init(raw);
    }
    if (err < 0) {
      m_swr.reset();
      throw std::runtime_error("cannot set up resampler: " +
                               detail::av_error(err));
    }

    av_channel_layout_uninit(&m_in_layout);
    av_channel_layout_copy(&m_in_layout, &frame->ch_layout);
    m_in_format = frame->format;
    m_in_rate = frame->sample_rate;
  }

  AudioFrame resample(const AVFrame* frame, double pts) {
    AudioFrame out;
    out.format = m_format;
    out.pts = pts;

    const int in_samples = frame != nullptr ? frame->nb_samples : 0;
    const int capacity = swr_get_out_samples(m_swr.get(), in_samples);
    if (capacity <= 0) {
      return out;
    }
    out.data.resize(static_cast<std::size_t>(capacity) * m_frame_bytes);

    std::uint8_t* dst = out.data.data();
    const int produced = swr_convert(
        m_swr.get(), &dst, capacity,
        frame != nullptr ? const_cast<const std::uint8_t**>(frame->extended_data)
                         : nullptr,
        in_samples);
    if (produced < 0) {
      throw std::runtime_error("swr_convert: " + detail::av_error(produced));
    }
    out.frames = static_cast<std::size_t>(produced);
    out.data.resize(out.frames * m_frame_bytes);
    return out;
  }

  double resampler_delay() const {
    return static_cast<double>(swr_get_delay(m_swr.get(), 1'000'000)) / 1e6;
  }

  bool trim(AudioFrame& out) const {
    const double end = out.pts + static_cast<double>(out.frames) / m_format.sample_rate;
    if (end <= m_discard_before) {
      return false;
    }
    if (out.pts < m_discard_before) {
      const auto skip = std::min<std::size_t>(
          out.frames, static_cast<std::size_t>((m_discard_before - out.pts) *
                                               m_format.sample_rate));
      out.data.erase(out.data.begin(),
                     out.data.begin() + static_cast<std::ptrdiff_t>(skip * m_frame_bytes));
      out.frames -= skip;
      out.pts += static_cast<double>(skip) / m_format.sample_rate;
    }
    return out.frames > 0;
  }

  std::optional<AudioFrame> receive() {
    while (m_core.receive(m_frame.get())) {
      const AVFrame* frame = m_frame.get();
      ensure_resampler(frame);

      const std::int64_t ts = detail::frame_timestamp(frame);
      const double in_pts = ts != AV_NOPTS_VALUE
                                ? detail::to_seconds(ts, m_core.stream())
                                : m_next_pts;
      m_next_pts = in_pts + static_cast<double>(frame->nb_samples) /
                                frame->sample_rate;

      AudioFrame out = resample(frame, in_pts - resampler_delay());
      if (trim(out)) {
        return out;
      }
    }

    if (m_core.finished() && !m_tail_done) {
      m_tail_done = true;
      if (m_swr) {
        AudioFrame out = resample(nullptr, m_next_pts - resampler_delay());
        if (trim(out)) {
          return out;
        }
      }
    }
    return std::nullopt;
  }

  void flush(double discard_before) {
    m_core.flush();
    if (m_swr) {
      swr_close(m_swr.get());
      swr_init(m_swr.get());
    }
    m_discard_before = discard_before;
    m_next_pts = std::max(0.0, discard_before);
    m_tail_done = false;
  }
};

AudioDecoder::AudioDecoder(const Demuxer& demuxer, const AudioFormat& output)
    : m_impl(std::make_unique<Impl>(
          detail::Access::stream(demuxer, StreamType::Audio), output)) {}

AudioDecoder::~AudioDecoder() = default;
AudioDecoder::AudioDecoder(AudioDecoder&&) noexcept = default;
AudioDecoder& AudioDecoder::operator=(AudioDecoder&&) noexcept = default;

const AudioFormat& AudioDecoder::format() const noexcept {
  return m_impl->m_format;
}

void AudioDecoder::send(const Packet& packet) { m_impl->m_core.send(packet); }
void AudioDecoder::send_eof() { m_impl->m_core.send_eof(); }

std::optional<AudioFrame> AudioDecoder::receive() { return m_impl->receive(); }

bool AudioDecoder::finished() const noexcept {
  return m_impl->m_core.finished() && m_impl->m_tail_done;
}

void AudioDecoder::flush(double discard_before) {
  m_impl->flush(discard_before);
}

}
