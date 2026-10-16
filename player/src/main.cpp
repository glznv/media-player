#include <cstdio>
#include <cstring>
#include <exception>
#include <string>

#include "player.hpp"

namespace {

void usage(const char* self) {
  std::fprintf(stderr,
               "usage: %s FILE [--audio-device DEVICE]\n"
               "  FILE                 mp4 or mkv file\n"
               "  --audio-device DEV   ALSA device (default: \"default\"),\n"
               "                       see `aplay -L`, e.g. plughw:1,0\n",
               self);
}

}

int main(int argc, char** argv) {
  player::Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    }
    if (arg == "--audio-device" && i + 1 < argc) {
      options.audio_device = argv[++i];
    } else if (options.path.empty() && arg.rfind("--", 0) != 0) {
      options.path = arg;
    } else {
      usage(argv[0]);
      return 1;
    }
  }
  if (options.path.empty()) {
    usage(argv[0]);
    return 1;
  }

  try {
    player::Player player(options);
    std::printf("Space: pause   Left/Right: seek 5 s   Esc/Q: quit\n");
    player.run();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "\nerror: %s\n", e.what());
    return 1;
  }
  return 0;
}
