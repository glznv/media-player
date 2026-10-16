#include <cstdio>
#include <stdexcept>

#include "internal.hpp"

namespace media::detail {

CodecContextPtr open_decoder(const StreamParams& stream) {
  const AVCodec* codec = avcodec_find_decoder(stream.codecpar->codec_id);
  if (codec == nullptr) {
    throw std::runtime_error(std::string("no decoder for codec ") +
                             avcodec_get_name(stream.codecpar->codec_id));
  }

  CodecContextPtr ctx(avcodec_alloc_context3(codec));
  if (!ctx) {
    throw std::bad_alloc();
  }
  int err = avcodec_parameters_to_context(ctx.get(), stream.codecpar);
  if (err < 0) {
    throw std::runtime_error("avcodec_parameters_to_context: " +
                             av_error(err));
  }
  ctx->pkt_timebase = stream.time_base;
  ctx->thread_count = 0;

  err = avcodec_open2(ctx.get(), codec, nullptr);
  if (err < 0) {
    throw std::runtime_error("avcodec_open2: " + av_error(err));
  }
  return ctx;
}

DecoderCore::DecoderCore(const StreamParams& stream, StreamType type)
    : m_stream(stream), m_type(type), m_ctx(open_decoder(stream)) {
  m_stream.codecpar = nullptr;
}

void DecoderCore::send(const Packet& packet) {
  const Access::PacketImpl& impl = Access::impl(packet);
  if (impl.type != m_type || m_eof_sent) {
    return;
  }
  const int err = avcodec_send_packet(m_ctx.get(), impl.packet);
  if (err == AVERROR(EAGAIN)) {
    throw std::logic_error(
        "decoder is full: call receive() until nullopt before send()");
  }
  if (err < 0) {
    std::fprintf(stderr, "decode error: %s\n", av_error(err).c_str());
  }
}

void DecoderCore::send_eof() {
  if (!m_eof_sent) {
    avcodec_send_packet(m_ctx.get(), nullptr);
    m_eof_sent = true;
  }
}

bool DecoderCore::receive(AVFrame* frame) {
  if (m_finished) {
    return false;
  }
  av_frame_unref(frame);
  const int err = avcodec_receive_frame(m_ctx.get(), frame);
  if (err == 0) {
    return true;
  }
  if (err == AVERROR_EOF) {
    m_finished = true;
  } else if (err != AVERROR(EAGAIN)) {
    std::fprintf(stderr, "decode error: %s\n", av_error(err).c_str());
  }
  return false;
}

void DecoderCore::flush() {
  avcodec_flush_buffers(m_ctx.get());
  m_eof_sent = false;
  m_finished = false;
}

}
