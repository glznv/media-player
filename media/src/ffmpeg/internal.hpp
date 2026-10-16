#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libavutil/version.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#include <media/demuxer.hpp>
#include <media/packet.hpp>
#include <media/video_frame.hpp>

#include <memory>
#include <string>

namespace media {

struct Packet::Impl {
  AVPacket* packet = nullptr;
  StreamType type = StreamType::Video;

  Impl() : packet(av_packet_alloc()) {}
  ~Impl() { av_packet_free(&packet); }
  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;
};

struct VideoFrame::Impl {
  AVFrame* frame = nullptr;
  double pts = 0.0;
  double duration = 0.0;

  ~Impl() { av_frame_free(&frame); }
};

namespace detail {

struct StreamParams {
  const AVCodecParameters* codecpar = nullptr;
  AVRational time_base{0, 1};
  double start_offset = 0.0;
};

struct Access {
  using PacketImpl = Packet::Impl;
  using FrameImpl = VideoFrame::Impl;

  static StreamParams stream(const Demuxer& demuxer, StreamType type);

  static Packet make_packet(std::unique_ptr<Packet::Impl> impl) noexcept {
    return Packet(std::move(impl));
  }
  static const Packet::Impl& impl(const Packet& packet) noexcept {
    return *packet.m_impl;
  }
  static VideoFrame make_frame(std::unique_ptr<VideoFrame::Impl> impl) noexcept {
    return VideoFrame(std::move(impl));
  }
};

inline std::string av_error(int err) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(err, buf, sizeof(buf));
  return buf;
}

struct CodecContextDeleter {
  void operator()(AVCodecContext* ctx) const noexcept {
    avcodec_free_context(&ctx);
  }
};
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;

CodecContextPtr open_decoder(const StreamParams& stream);

class DecoderCore {
 public:
  DecoderCore(const StreamParams& stream, StreamType type);

  void send(const Packet& packet);
  void send_eof();
  bool receive(AVFrame* frame);
  void flush();

  [[nodiscard]] bool finished() const noexcept { return m_finished; }
  [[nodiscard]] const StreamParams& stream() const noexcept { return m_stream; }
  [[nodiscard]] AVCodecContext* context() const noexcept { return m_ctx.get(); }

 private:
  StreamParams m_stream;
  StreamType m_type;
  CodecContextPtr m_ctx;
  bool m_eof_sent = false;
  bool m_finished = false;
};

// AVFrame::duration appeared in FFmpeg 6.0; before it was pkt_duration,
// which 7.0 removed.
inline std::int64_t frame_duration(const AVFrame* frame) {
#if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(58, 2, 100)
  return frame->duration;
#else
  return frame->pkt_duration;
#endif
}

inline std::int64_t frame_timestamp(const AVFrame* frame) {
  return frame->best_effort_timestamp != AV_NOPTS_VALUE
             ? frame->best_effort_timestamp
             : frame->pts;
}

inline double to_seconds(std::int64_t ts, const StreamParams& stream) {
  return static_cast<double>(ts) * av_q2d(stream.time_base) -
         stream.start_offset;
}

}
}
