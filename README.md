# media-player

## Модули

Проект состоит из следующих модулей:

| Модуль   | Назначение |
|----------|------------|
| `render` | Отрисовка кадра в окне и получение событий от клавиатуры |
| `audio`  | Воспроизведение PCM на устройстве ALSA |
| `media`  | Демультиплексирование контейнера и декодирование медиапотоков |
| `player` | Сам плеер: синхронизирует воспроизведение аудио/видео, управляет потоками, содержит `main_loop` |

## Требования

- FFmpeg версии 5.1 и выше
- Поддержка X11 и OpenGL в системе

## Сборка

```bash
cmake --preset player-with-test-programs
cmake --build --preset player-with-test-programs
```

Доступны два пресета:

- `player-only` — только плеер;
- `player-with-test-programs` — плеер и тестовые программы для каждого модуля.

Все исполняемые файлы находятся в `./build/bin/`.

## Запуск

```bash
./build/bin/player ../mttech/samples/Madagascar.mp4 --audio-device hw:1,0
```

### Выбор аудиоустройства

Значение `--audio-device` можно получить с помощью `aplay -l`:

```console
$ aplay -l
**** List of PLAYBACK Hardware Devices ****
card 0: Generic [HD-Audio Generic], device 3: HDMI 0 [HDMI 0]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 0: Generic [HD-Audio Generic], device 7: HDMI 1 [HDMI 1]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
card 1: Generic_1 [HD-Audio Generic], device 0: ALC287 Analog [ALC287 Analog]
  Subdevices: 1/1
  Subdevice #0: subdevice #0
```

Устройство задаётся в формате `hw:<card>,<device>`. В примере выше аналоговый выход — это `card 1`, `device 0`, то есть `hw:1,0`.

## Тестовые программы

Каждый модуль можно протестировать отдельно (требуется сборка с пресетом `player-with-test-programs`).

### test_render

Выводит в окно изображение в формате YUV420.

```bash
./build/bin/test_render 1280x720 ../mttech/samples/frame.yuv
```
Получить тестовое изображние из видео можно командой:

```bash
ffmpeg -ss 10 -i ../mttech/samples/Madagascar.mp4 -frames:v 1 -vf scale=1280:720 -pix_fmt yuv420p -f rawvideo frame.yuv
```

### test_audio

Проигрывает PCM-файл.

```bash
./build/bin/test_audio ../mttech/samples/out.pcm hw:1,0
```

Получить тестовый pcm из контейнера можно командой:

```bash
ffmpeg -i ../mttech/samples/Madagascar.mp4 -vn -f s16le -acodec pcm_s16le -ar 48000 -ac 2 out.pcm
```

### test_media

Открывает медиаконтейнер, декодирует потоки и сохраняет raw-аудио и видео.

```bash
./build/bin/test_media ../mttech/samples/Madagascar.mp4 out --seek 10 --duration 10
```

Полученные файлы можно просмотреть с помощью `ffplay`:

```bash
# аудио
ffplay -f s16le -sample_rate 48000 -ch_layout stereo out.pcm

# видео
ffplay -f rawvideo -pixel_format yuv420p -video_size 1280x720 -framerate 30 out.yuv
```

## Параметры видеофайла

Посмотреть параметры видео можно через `ffprobe`:

```console
$ ffprobe -hide_banner ../mttech/samples/Madagascar.mp4
Input #0, mov,mp4,m4a,3gp,3g2,mj2, from '../mttech/samples/Madagascar.mp4':
  Metadata:
    major_brand     : isom
    minor_version   : 512
    compatible_brands: isomiso6iso2avc1mp41
    encoder         : Lavf59.27.100
  Duration: 00:01:39.77, start: 0.000000, bitrate: 1363 kb/s
  Stream #0:0[0x1](und): Video: h264 (Main) (avc1 / 0x31637661), yuv420p(tv, bt709, progressive), 1280x720 [SAR 1:1 DAR 16:9], 1232 kb/s, 23.98 fps, 23.98 tbr, 24k tbn, start 0.041708 (default)
    Metadata:
      handler_name    : ISO Media file produced by Google Inc.
  Stream #0:1[0x2](eng): Audio: aac (LC) (mp4a / 0x6134706D), 44100 Hz, stereo, fltp, 127 kb/s (default)
    Metadata:
      handler_name    : ISO Media file produced by Google Inc.
```
