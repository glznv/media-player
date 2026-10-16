#include <media/demuxer.hpp>

#include <stdexcept>

#include "internal.hpp"

namespace media {

namespace {

struct FormatContextDeleter {
  void operator()(AVFormatContext* ctx) const noexcept {
    avformat_close_input(&ctx);
  }
};

int find_stream(AVFormatContext* ctx, AVMediaType type, int related) {
  const int index = av_find_best_stream(ctx, type, -1, related, nullptr, 0);
  if (index < 0) {
    return -1;
  }
  if ((ctx->streams[index]->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0) {
    return -1;
  }
  return index;
}

}

struct Demuxer::Impl {
  std::unique_ptr<AVFormatContext, FormatContextDeleter> m_ctx;
  int m_video_index = -1;
  int m_audio_index = -1;
  double m_start_offset = 0.0;
  double m_duration = 0.0;
  std::optional<VideoStreamInfo> m_video;
  std::optional<AudioStreamInfo> m_audio;

  explicit Impl(const std::string& path) {
    AVFormatContext* raw = nullptr;
    int err = avformat_open_input(&raw, path.c_str(), nullptr, nullptr);
    if (err < 0) {
      throw std::runtime_error("cannot open \"" + path +
                               "\": " + detail::av_error(err));
    }
    m_ctx.reset(raw);

    err = avformat_find_stream_info(raw, nullptr);
    if (err < 0) {
      throw std::runtime_error("cannot read stream info: " +
                               detail::av_error(err));
    }

    m_video_index = find_stream(raw, AVMEDIA_TYPE_VIDEO, -1);
    m_audio_index = find_stream(raw, AVMEDIA_TYPE_AUDIO, m_video_index);
    if (m_video_index < 0 && m_audio_index < 0) {
      throw std::runtime_error("no audio or video streams in \"" + path + "\"");
    }

    for (unsigned i = 0; i < raw->nb_streams; ++i) {
      const int index = static_cast<int>(i);
      if (index != m_video_index && index != m_audio_index) {
        raw->streams[i]->discard = AVDISCARD_ALL;
      }
    }

    if (raw->start_time != AV_NOPTS_VALUE) {
      m_start_offset = static_cast<double>(raw->start_time) / AV_TIME_BASE;
    }
    if (raw->duration != AV_NOPTS_VALUE) {
      m_duration = static_cast<double>(raw->duration) / AV_TIME_BASE;
    }

    if (m_video_index >= 0) {
      AVStream* stream = raw->streams[m_video_index];
      const AVCodecParameters* par = stream->codecpar;
      m_video = VideoStreamInfo{
          avcodec_get_name(par->codec_id), par->width, par->height,
          av_q2d(av_guess_frame_rate(raw, stream, nullptr))};
    }
    if (m_audio_index >= 0) {
      const AVCodecParameters* par = raw->streams[m_audio_index]->codecpar;
      m_audio = AudioStreamInfo{avcodec_get_name(par->codec_id),
                                par->ch_layout.nb_channels, par->sample_rate};
    }
  }

  std::optional<Packet> read() {
    auto impl = std::make_unique<detail::Access::PacketImpl>();
    if (impl->packet == nullptr) {
      throw std::bad_alloc();
    }
    for (;;) {
      const int err = av_read_frame(m_ctx.get(), impl->packet);
      if (err == AVERROR_EOF) {
        return std::nullopt;
      }
      if (err < 0) {
        throw std::runtime_error("read error: " + detail::av_error(err));
      }
      const int index = impl->packet->stream_index;
      if (index == m_video_index || index == m_audio_index) {
        impl->type =
            index == m_video_index ? StreamType::Video : StreamType::Audio;
        return detail::Access::make_packet(std::move(impl));
      }
      av_packet_unref(impl->packet);
    }
  }

  bool seek(double seconds) {
    const auto target = static_cast<std::int64_t>(
        (seconds + m_start_offset) * AV_TIME_BASE);
    if (avformat_seek_file(m_ctx.get(), -1, INT64_MIN, target, target, 0) >=
        0) {
      return true;
    }
    return av_seek_frame(m_ctx.get(), -1, target, AVSEEK_FLAG_BACKWARD) >= 0;
  }
};

Demuxer::Demuxer(const std::string& path)
    : m_impl(std::make_unique<Impl>(path)) {}

Demuxer::~Demuxer() = default;
Demuxer::Demuxer(Demuxer&&) noexcept = default;
Demuxer& Demuxer::operator=(Demuxer&&) noexcept = default;

const std::optional<VideoStreamInfo>& Demuxer::video() const noexcept {
  return m_impl->m_video;
}

const std::optional<AudioStreamInfo>& Demuxer::audio() const noexcept {
  return m_impl->m_audio;
}

double Demuxer::duration() const noexcept { return m_impl->m_duration; }

std::optional<Packet> Demuxer::read() { return m_impl->read(); }

bool Demuxer::seek(double seconds) { return m_impl->seek(seconds); }

detail::StreamParams detail::Access::stream(const Demuxer& demuxer,
                                            StreamType type) {
  const Demuxer::Impl& impl = *demuxer.m_impl;
  const int index =
      type == StreamType::Video ? impl.m_video_index : impl.m_audio_index;
  if (index < 0) {
    throw std::runtime_error(type == StreamType::Video
                                 ? "file has no video stream"
                                 : "file has no audio stream");
  }
  const AVStream* stream = impl.m_ctx->streams[index];
  return StreamParams{stream->codecpar, stream->time_base,
                      impl.m_start_offset};
}

}
