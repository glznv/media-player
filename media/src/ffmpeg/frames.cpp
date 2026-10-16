#include "internal.hpp"

namespace media {

std::size_t bytes_per_frame(const AudioFormat& format) noexcept {
  return 2 * static_cast<std::size_t>(format.channels);
}

Packet::Packet(std::unique_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}
Packet::~Packet() = default;
Packet::Packet(Packet&&) noexcept = default;
Packet& Packet::operator=(Packet&&) noexcept = default;

StreamType Packet::stream() const noexcept { return m_impl->type; }

VideoFrame::VideoFrame(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl)) {}
VideoFrame::~VideoFrame() = default;
VideoFrame::VideoFrame(VideoFrame&&) noexcept = default;
VideoFrame& VideoFrame::operator=(VideoFrame&&) noexcept = default;

int VideoFrame::width() const noexcept { return m_impl->frame->width; }
int VideoFrame::height() const noexcept { return m_impl->frame->height; }

const std::uint8_t* VideoFrame::plane(int index) const noexcept {
  return m_impl->frame->data[index];
}

int VideoFrame::stride(int index) const noexcept {
  return m_impl->frame->linesize[index];
}

double VideoFrame::pts() const noexcept { return m_impl->pts; }
double VideoFrame::duration() const noexcept { return m_impl->duration; }

}
