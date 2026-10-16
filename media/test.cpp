#include <media/audio_decoder.hpp>
#include <media/demuxer.hpp>
#include <media/video_decoder.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <memory>
#include <optional>
#include <string>

namespace {

struct FileCloser {
  void operator()(std::FILE* f) const noexcept { std::fclose(f); }
};
using File = std::unique_ptr<std::FILE, FileCloser>;

File open_out(const std::string& path) {
  File f(std::fopen(path.c_str(), "wb"));
  if (!f) throw std::runtime_error("cannot create " + path);
  return f;
}

void write_video(std::FILE* out, const media::VideoFrame& frame) {
  for (int i = 0; i < 3; ++i) {
    const int w = i > 0 ? (frame.width() + 1) / 2 : frame.width();
    const int h = i > 0 ? (frame.height() + 1) / 2 : frame.height();
    for (int y = 0; y < h; ++y) {
      std::fwrite(frame.plane(i) + static_cast<std::ptrdiff_t>(y) * frame.stride(i),
                  1, static_cast<std::size_t>(w), out);
    }
  }
}

constexpr int kPrintFirst = 12;

}

int main(int argc, char** argv) try {
  if (argc < 3) {
    std::fprintf(stderr,
                 "usage: %s input.(mp4|mkv) out_prefix [--seek S] "
                 "[--duration D]\n",
                 argv[0]);
    return 1;
  }
  const std::string input = argv[1];
  const std::string prefix = argv[2];
  double seek = 0.0;
  double duration = 10.0;
  for (int i = 3; i + 1 < argc; i += 2) {
    if (std::strcmp(argv[i], "--seek") == 0) seek = std::atof(argv[i + 1]);
    if (std::strcmp(argv[i], "--duration") == 0) duration = std::atof(argv[i + 1]);
  }

  media::Demuxer demuxer(input);

  std::printf("file: %s, duration %.2f s\n", input.c_str(), demuxer.duration());
  if (const auto& v = demuxer.video()) {
    std::printf("video: %s %dx%d %.3f fps\n", v->codec.c_str(), v->width,
                v->height, v->fps);
  }
  if (const auto& a = demuxer.audio()) {
    std::printf("audio: %s %d ch %d Hz\n", a->codec.c_str(), a->channels,
                a->sample_rate);
  }

  std::optional<media::VideoDecoder> video;
  std::optional<media::AudioDecoder> audio;
  if (demuxer.video()) video.emplace(demuxer);
  if (demuxer.audio()) {
    audio.emplace(demuxer, media::AudioFormat{2, 48000});
  }

  if (seek > 0.0) {
    if (!demuxer.seek(seek)) std::printf("seek failed, starting from 0\n");
    if (video) video->flush(seek);
    if (audio) audio->flush(seek);
  }
  const double end = seek + duration;

  File video_out;
  const File audio_out = audio ? open_out(prefix + ".pcm") : File{};

  int video_frames = 0;
  std::size_t audio_samples = 0;
  int audio_chunks = 0;
  bool video_done = !video;
  bool audio_done = !audio;
  int width = 0;
  int height = 0;

  auto drain_video = [&] {
    while (auto frame = video->receive()) {
      if (frame->pts() >= end) { video_done = true; continue; }
      if (!video_out) {
        width = frame->width();
        height = frame->height();
        video_out = open_out(prefix + ".yuv");
      }
      if (video_frames < kPrintFirst) {
        std::printf("  V #%-3d pts %8.3f  dur %.3f\n", video_frames,
                    frame->pts(), frame->duration());
      }
      write_video(video_out.get(), *frame);
      ++video_frames;
    }
    if (video->finished()) video_done = true;
  };

  auto drain_audio = [&] {
    while (auto chunk = audio->receive()) {
      if (chunk->pts >= end) { audio_done = true; continue; }
      if (audio_chunks < kPrintFirst) {
        std::printf("  A #%-3d pts %8.3f  %zu samples\n", audio_chunks,
                    chunk->pts, chunk->frames);
      }
      std::fwrite(chunk->data.data(), 1, chunk->data.size(), audio_out.get());
      audio_samples += chunk->frames;
      ++audio_chunks;
    }
    if (audio->finished()) audio_done = true;
  };

  while (!(video_done && audio_done)) {
    std::optional<media::Packet> packet = demuxer.read();
    if (!packet) {
      if (video) { video->send_eof(); drain_video(); video_done = true; }
      if (audio) { audio->send_eof(); drain_audio(); audio_done = true; }
      break;
    }
    if (packet->stream() == media::StreamType::Video && video && !video_done) {
      video->send(*packet);
      drain_video();
    } else if (packet->stream() == media::StreamType::Audio && audio &&
               !audio_done) {
      audio->send(*packet);
      drain_audio();
    }
  }

  std::printf("\nwritten: %d video frames, %.2f s of audio\n", video_frames,
              audio_samples / 48000.0);
  if (video_out) {
    std::printf("check video: ffplay -f rawvideo -pixel_format yuv420p "
                "-video_size %dx%d -framerate %.3f %s.yuv\n",
                width, height, demuxer.video()->fps, prefix.c_str());
  }
  if (audio_out) {
    std::printf("check audio: ./test_audio %s.pcm plughw:1,0\n", prefix.c_str());
  }
  return 0;
} catch (const std::exception& e) {
  std::fprintf(stderr, "error: %s\n", e.what());
  return 1;
}
