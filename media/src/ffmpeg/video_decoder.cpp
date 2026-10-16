#include <media/video_decoder.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "internal.hpp"

namespace media {

namespace {

struct FrameDeleter {
  void operator()(AVFrame* frame) const noexcept { av_frame_free(&frame); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

struct SwsDeleter {
  void operator()(SwsContext* ctx) const noexcept { sws_freeContext(ctx); }
};

}

struct VideoDecoder::Impl {
  detail::DecoderCore m_core;
  FramePtr m_frame{av_frame_alloc()};
  std::unique_ptr<SwsContext, SwsDeleter> m_sws;
  double m_discard_before;
  double m_fallback_duration;
  double m_next_pts = 0.0;

  Impl(const detail::StreamParams& stream, double fps)
      : m_core(stream, StreamType::Video),
        m_discard_before(-std::numeric_limits<double>::infinity()),
        m_fallback_duration(fps > 0.0 ? 1.0 / fps : 1.0 / 25.0) {
    if (!m_frame) {
      throw std::bad_alloc();
    }
  }

  std::unique_ptr<detail::Access::FrameImpl> convert(AVFrame* src) {
    auto out = std::make_unique<detail::Access::FrameImpl>();
    out->frame = av_frame_alloc();
    if (out->frame == nullptr) {
      throw std::bad_alloc();
    }

    if (src->format == AV_PIX_FMT_YUV420P || src->format == AV_PIX_FMT_YUVJ420P) {
      av_frame_move_ref(out->frame, src);
      return out;
    }

    m_sws.reset(sws_getCachedContext(
        m_sws.release(), src->width, src->height,
        static_cast<AVPixelFormat>(src->format), src->width, src->height,
        AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr));
    if (!m_sws) {
      throw std::runtime_error("sws_getCachedContext failed");
    }

    out->frame->format = AV_PIX_FMT_YUV420P;
    out->frame->width = src->width;
    out->frame->height = src->height;
    int err = av_frame_get_buffer(out->frame, 0);
    if (err < 0) {
      throw std::runtime_error("av_frame_get_buffer: " +
                               detail::av_error(err));
    }
    err = sws_scale_frame(m_sws.get(), out->frame, src);
    if (err < 0) {
      throw std::runtime_error("sws_scale_frame: " + detail::av_error(err));
    }
    return out;
  }

  std::optional<VideoFrame> receive() {
    while (m_core.receive(m_frame.get())) {
      AVFrame* frame = m_frame.get();
      const detail::StreamParams& stream = m_core.stream();

      const std::int64_t ts = detail::frame_timestamp(frame);
      const double pts =
          ts != AV_NOPTS_VALUE ? detail::to_seconds(ts, stream) : m_next_pts;
      const std::int64_t frame_duration = detail::frame_duration(frame);
      const double duration =
          frame_duration > 0
              ? static_cast<double>(frame_duration) * av_q2d(stream.time_base)
              : m_fallback_duration;
      m_next_pts = pts + duration;

      if (pts + duration <= m_discard_before) {
        continue;
      }

      auto impl = convert(frame);
      impl->pts = pts;
      impl->duration = duration;
      return detail::Access::make_frame(std::move(impl));
    }
    return std::nullopt;
  }
};

VideoDecoder::VideoDecoder(const Demuxer& demuxer)
    : m_impl(std::make_unique<Impl>(
          detail::Access::stream(demuxer, StreamType::Video),
          demuxer.video() ? demuxer.video()->fps : 0.0)) {}

VideoDecoder::~VideoDecoder() = default;
VideoDecoder::VideoDecoder(VideoDecoder&&) noexcept = default;
VideoDecoder& VideoDecoder::operator=(VideoDecoder&&) noexcept = default;

void VideoDecoder::send(const Packet& packet) { m_impl->m_core.send(packet); }
void VideoDecoder::send_eof() { m_impl->m_core.send_eof(); }

std::optional<VideoFrame> VideoDecoder::receive() { return m_impl->receive(); }

bool VideoDecoder::finished() const noexcept {
  return m_impl->m_core.finished();
}

void VideoDecoder::flush(double discard_before) {
  m_impl->m_core.flush();
  m_impl->m_discard_before = discard_before;
  m_impl->m_next_pts = std::max(0.0, discard_before);
}

}
