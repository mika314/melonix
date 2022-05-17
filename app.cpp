#include "app.hpp"
#include <SDL.h>
#include <algorithm>
#include <functional>
#include <log/log.hpp>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

auto App::draw() -> void
{
  std::function<void(void)> postponedAction = nullptr;

  if (ImGui::BeginMainMenuBar())
  {
    if (ImGui::BeginMenu("File"))
    {
      if (ImGui::MenuItem("Open"))
      {
        LOG("Open");
        postponedAction = [&]() { ImGui::OpenPopup("FileOpen"); };
      }
      if (ImGui::MenuItem("Save")) {}
      if (ImGui::MenuItem("Quit")) {}
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
  if (postponedAction)
    postponedAction();

  if (fileOpen.draw())
  {
    LOG("open", fileOpen.getSelectedFile());
    specCache = nullptr;
    spec = nullptr;
    audio = nullptr;
    startTime = 0.;
    rangeTime = 10.;
    cursor = 0;
    load(fileOpen.getSelectedFile());
    calcPicks();
    auto want = [&]() {
      SDL_AudioSpec want;
      want.freq = sampleRate;
      want.format = AUDIO_F32LSB;
      want.channels = 1;
      want.samples = 1024;
      return want;
    }();
    SDL_AudioSpec have;
    audio = std::make_unique<sdl::Audio>(nullptr, false, &want, &have, 0, [&](Uint8 *stream, int len) {
      auto w = reinterpret_cast<float *>(stream);

      if (cursor < 0 || cursor >= static_cast<int>(size))
      {
        isAudioPlaying = false;
        audio->pause(true);
        return;
      }

      auto dur = std::min(len / static_cast<int>(sizeof(float)), static_cast<int>(size) - cursor);
      if (dur <= 0)
      {
        isAudioPlaying = false;
        audio->pause(true);
        return;
      }

      std::copy(data + cursor, data + cursor + dur, w);
      cursor += dur;
    });
    spec = std::make_unique<Spec>(std::span<float>{data, data + size});
  }

  {
    ImGui::Begin("Control Center");
    ImGui::Text("<%.2f %.2f %.2f>", startTime, sampleRate == 0 ? 0. : 1. * cursor / sampleRate, startTime + rangeTime);
    ImGui::Checkbox("Follow", &followMode);
    ImGui::SameLine();
    // play/stop button
    if (ImGui::Button(isAudioPlaying ? "Stop" : "Play"))
      togglePlay();
    // brightnes
    ImGui::SliderFloat("Brightness", &brightness, 0.0f, 100.0f);
    float newK = pow(2, brightness / 10 + 9);
    if (k != newK)
    {
      k = newK;
      specCache = nullptr;
    }
    ImGui::Text("FPS: %.1f (%.3f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);
    ImGui::End();
  }
  if (audio)
  {
    audio->lock();
    displayCursor = cursor;
    audio->unlock();
    if (displayCursor > (startTime + rangeTime) * sampleRate && isAudioPlaying)
      followMode = true;
    if (followMode)
    {
      const auto cursorSec = 1. * displayCursor / sampleRate;
      const auto desieredStart = cursorSec - rangeTime / 5;
      const auto newStart =
        (std::abs(desieredStart - startTime) > 4 * 1024. / sampleRate) ? (startTime + (desieredStart - startTime) * 0.2) : desieredStart;
      if (newStart != startTime)
      {
        startTime = newStart;
        waveformCache.clear();
      }
    }
  }
}

auto App::calcPicks() -> void
{
  picks.clear();
  auto lvl = 0U;

  if (size <= (1 << (lvl + 1)))
    return;
  while (picks.size() <= lvl)
    picks.push_back({});
  for (auto i = 0U; i < size / (1 << (lvl + 1)); ++i)
  {
    const auto min = std::min(data[i * 2], data[i * 2 + 1]);
    const auto max = std::max(data[i * 2], data[i * 2 + 1]);
    picks[lvl].push_back(std::make_pair(min, max));
  }

  for (;;)
  {
    ++lvl;
    if (size <= (1 << (lvl + 1)))
      break;
    while (picks.size() <= lvl)
      picks.push_back({});
    for (auto i = 0U; i < size / (1 << (lvl + 1)); ++i)
    {
      const auto min = std::min(picks[lvl - 1][i * 2].first, picks[lvl - 1][i * 2 + 1].first);
      const auto max = std::max(picks[lvl - 1][i * 2].second, picks[lvl - 1][i * 2 + 1].second);
      picks[lvl].push_back(std::make_pair(min, max));
    }
  }
  waveformCache.clear();
}

auto App::getMinMaxFromRange(int start, int end) -> std::pair<float, float>
{
  if (start >= end)
  {
    if (start >= 0 && start < static_cast<int>(size))
      return {data[start], data[start]};
    return {0.f, 0.f};
  }

  if (start < 0 || end < 0)
    return {0.f, 0.f};

  if (start >= static_cast<int>(size) || end >= static_cast<int>(size))
    return {0.f, 0.f};

  if (end - start == 1)
    return {data[start], data[start]};

  // calculate level
  const auto lvl = static_cast<size_t>(std::log2(end - start));
  // Get the minimum and maximum from the level
  const auto lvlStart = start / (1 << lvl);
  auto minMax = [&]() {
    if (lvl - 1 >= picks.size())
      return std::pair{0.f, 0.f};
    if (lvlStart >= static_cast<int>(picks[lvl - 1].size()))
      return std::pair{0.f, 0.f};
    return picks[lvl - 1][lvlStart];
  }();
  // Get left range
  const auto leftEnd = lvlStart * (1 << lvl);
  if (leftEnd >= start)
  {
    const auto leftMinMax = getMinMaxFromRange(start, leftEnd);
    minMax.first = std::min(minMax.first, leftMinMax.first);
    minMax.second = std::max(minMax.second, leftMinMax.second);
  }
  // Get right range
  const auto rightStart = (lvlStart + 1) * (1 << lvl);
  if (rightStart < end)
  {
    const auto rightMinMax = getMinMaxFromRange(rightStart, end);
    minMax.first = std::min(minMax.first, rightMinMax.first);
    minMax.second = std::max(minMax.second, rightMinMax.second);
  }
  return minMax;
}

auto App::glDraw() -> void
{
  const auto notesRange = 60;
  const auto startFreq = 55.;

  ImGuiIO &io = ImGui::GetIO();

  const auto Height = io.DisplaySize.y;
  const auto Width = io.DisplaySize.x;

  // Enable alpha blending
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glViewport(0, 0, (int)io.DisplaySize.x, static_cast<int>(.1 * Height - 20));
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, Width, 1.3f, -1.3f, -1, 1);
  // Our state
  ImVec4 clearColor = ImVec4(0.f, 0.f, 0.f, 1.f);
  glClearColor(clearColor.x * clearColor.w, clearColor.y * clearColor.w, clearColor.z * clearColor.w, clearColor.w);
  glClear(GL_COLOR_BUFFER_BIT);

  if (waveformCache.size() != Width)
    waveformCache.clear();

  if (waveformCache.empty())
  {
    for (auto x = 0; x < Width; ++x)
    {
      const auto left = (1. * x / Width * rangeTime + startTime) * sampleRate;
      const auto right = (1. * (x + 1) / Width * rangeTime + startTime) * sampleRate;
      auto minMax = getMinMaxFromRange(left, right);
      waveformCache.push_back(minMax);
    }
  }

  // draw waveform
  glColor3f(1.f, 0.f, 1.f);
  glBegin(GL_LINE_STRIP);
  for (auto x = 0; x < Width; ++x)
  {
    const auto minMax = waveformCache[x];
    glVertex2f(x, minMax.first);
    glVertex2f(x + 1, minMax.second);
  }
  glEnd();

  // draw spectogram
  glViewport(0, static_cast<int>(.1 * Height - 20), (int)io.DisplaySize.x, static_cast<int>(Height - 20));
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, Width, -.05, 1.05f, -1, 1);
  glEnable(GL_TEXTURE_1D);
  glColor3f(1.f, 1.f, 1.f);

  auto step = pow(2., 1. / 12.);

  for (auto x = 0; x < Width; ++x)
  {
    auto texture = getTex(startTime + x * rangeTime / Width);
    glBindTexture(GL_TEXTURE_1D, texture);

    glBegin(GL_QUADS);

    auto freq = startFreq / sampleRate * 2.;
    for (auto i = 0; i < notesRange; ++i)
    {
      glTexCoord1f(freq);
      glVertex2f(x, 1.f * i / notesRange);

      glTexCoord1f(freq * step);
      glVertex2f(x, 1.f * (i + 1) / notesRange);

      glTexCoord1f(freq * step);
      glVertex2f(x + 1.f, 1.f * (i + 1) / notesRange);

      glTexCoord1f(freq);
      glVertex2f(x + 1.f, 1.f * i / notesRange);

      freq *= step;
    }
    glEnd();
  }
  // draw piano
  glColor4f(1.f, 1.f, 1.f, .06f);
  glBindTexture(GL_TEXTURE_1D, pianoTexture);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  std::vector<std::array<unsigned char, 3>> pianoData;
  pianoData.resize(.9 * Height - 20);
  auto lastNote = 0U;
  for (auto i = 0U; i < pianoData.size(); ++i)
  {
    const auto tmp = i * notesRange + pianoData.size() / 2;
    const auto note = tmp / pianoData.size();
    auto isBlack = std::array{false, true, false, false, true, false, true, false, false, true, false, true}[note % 12];
    unsigned char c = (note == lastNote) ? (isBlack ? 128 : 255) : 0;
    pianoData[i] = std::array{c, c, c};
    lastNote = note;
  }
  glTexImage1D(GL_TEXTURE_1D,    // target
               0,                // level
               3,                // internalFormat
               pianoData.size(), // width
               0,                // border
               GL_RGB,           // format
               GL_UNSIGNED_BYTE, // type
               pianoData.data()  // data
  );

  glBegin(GL_QUADS);
  glTexCoord1f(0);
  glVertex2f(0, 0);
  glTexCoord1f(0);
  glVertex2f(Width, 0);
  glTexCoord1f(1);
  glVertex2f(Width, 1);
  glTexCoord1f(1);
  glVertex2f(0, 1);
  glEnd();

  glDisable(GL_TEXTURE_1D);

  glColor4f(1.f, 0.f, 0.5f, 0.25f);
  glBegin(GL_LINES);
  glVertex2f((1. * displayCursor / sampleRate - startTime) / rangeTime * Width, 0.f);
  glVertex2f((1. * displayCursor / sampleRate - startTime) / rangeTime * Width, 1.f);
  glEnd();
}

auto App::load(const std::string &path) -> void
{
  // get format from audio file
  AVFormatContext *format = avformat_alloc_context();
  if (avformat_open_input(&format, path.c_str(), NULL, NULL) != 0)
  {
    LOG("Could not open file", path);
    return;
  }
  if (avformat_find_stream_info(format, NULL) < 0)
  {
    LOG("Could not retrieve stream info from file", path);
    return;
  }

  // Find the index of the first audio stream
  int stream_index = -1;
  for (auto i = 0U; i < format->nb_streams; i++)
  {
    if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
      stream_index = i;
      break;
    }
  }
  if (stream_index == -1)
  {
    LOG("Could not retrieve audio stream from file", path);
    return;
  }
  AVStream *stream = format->streams[stream_index];

// disable warnings about API calls that are deprecated
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

  // find & open codec
  AVCodecContext *codec = stream->codec;
  if (avcodec_open2(codec, avcodec_find_decoder(codec->codec_id), NULL) < 0)
  {
    LOG("Failed to open decoder for stream #", stream_index, "in file", path);
    return;
  }

  // prepare resampler
  struct SwrContext *swr = swr_alloc();
  av_opt_set_int(swr, "in_channel_count", codec->channels, 0);
  av_opt_set_int(swr, "out_channel_count", 1, 0);
  av_opt_set_int(swr, "in_channel_layout", codec->channel_layout, 0);
  av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_MONO, 0);
  av_opt_set_int(swr, "in_sample_rate", codec->sample_rate, 0);
  sampleRate = codec->sample_rate;
  av_opt_set_int(swr, "out_sample_rate", sampleRate, 0);
  av_opt_set_sample_fmt(swr, "in_sample_fmt", codec->sample_fmt, 0);
  av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_FLT, 0);
  swr_init(swr);
  if (!swr_is_initialized(swr))
  {
    fprintf(stderr, "Resampler has not been properly initialized\n");
    return;
  }

  // prepare to read data
  AVPacket *packet = av_packet_alloc();

  AVFrame *frame = av_frame_alloc();
  if (!frame)
  {
    fprintf(stderr, "Error allocating the frame\n");
    return;
  }

  // iterate through frames
  data = nullptr;
  size = 0;
  while (av_read_frame(format, packet) >= 0)
  {
    // decode one frame
    int gotFrame;

    // skip frame if stream index does not match
    if (packet->stream_index != stream_index)
      continue;

    if (avcodec_decode_audio4(codec, frame, &gotFrame, packet) < 0)
    {
      break;
    }
    if (!gotFrame)
    {
      continue;
    }
    // resample frames
    float *buffer;
    av_samples_alloc((uint8_t **)&buffer, NULL, 1, frame->nb_samples, AV_SAMPLE_FMT_FLT, 0);
    int frame_count = swr_convert(swr, (uint8_t **)&buffer, frame->nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
    // append resampled frames to data
    data = (float *)realloc(data, (size + frame->nb_samples) * sizeof(float));
    memcpy(data + size, buffer, frame_count * sizeof(float));
    size += frame_count;
  }

// enable warnings about API calls that are deprecated
#pragma GCC diagnostic pop

  // clean up
  av_packet_unref(packet);
  av_frame_free(&frame);
  swr_free(&swr);
  avcodec_close(codec);
  avformat_free_context(format);

  LOG("File loaded", path, "duration", 1. * size / sampleRate, "smaple rate", sampleRate);
}

auto App::mouseMotion(int x, int /*y*/, int dx, int dy, uint32_t state) -> void
{
  if (size == 0)
    return;

  ImGuiIO &io = ImGui::GetIO();
  const auto Width = io.DisplaySize.x;
  if (state & SDL_BUTTON_RMASK)
  {
    auto modState = SDL_GetModState();
    const auto leftLimit = std::max(-rangeTime * 0.5, -.5 * size / sampleRate);
    const auto rightLimit = std::min(size / sampleRate + rangeTime * 0.5, 1.5 * size / sampleRate);
    if ((modState & (KMOD_LCTRL | KMOD_RCTRL)) == 0)
    {
      // panning
      const auto dt = 1. * dx * rangeTime / Width;
      auto newStartTime = startTime - dt;

      if (newStartTime < leftLimit)
        newStartTime = leftLimit;
      if (newStartTime + rangeTime > rightLimit)
        newStartTime = rightLimit - rangeTime;
      startTime = newStartTime;
      waveformCache.clear();
      followMode = false;
    }
    else
    {
      // zoom in or zoom out
      const auto zoom = 1. + 0.01 * dy;
      const auto cursorPos = 1. * x / Width * rangeTime + startTime;
      const auto newStartTime = (startTime - cursorPos) * zoom + cursorPos;
      const auto newEndTime = (startTime + rangeTime - cursorPos) * zoom + cursorPos;
      startTime = (newStartTime >= leftLimit && newStartTime <= rightLimit) ? newStartTime : startTime;
      if (newEndTime >= leftLimit && newEndTime <= rightLimit)
        rangeTime = newEndTime - startTime;
      else if (newEndTime < leftLimit)
        rangeTime = 10.;
      else if (newEndTime > rightLimit)
        rangeTime = rightLimit - startTime;
      waveformCache.clear();
      specCache = nullptr;
      followMode = false;
    }
  }
  else if (state & SDL_BUTTON_LMASK)
  {
    if (!audio)
      return;
    audio->lock();
    cursor = [&]() {
      const auto tmp = std::clamp(static_cast<int>(x * rangeTime * sampleRate / Width + startTime * sampleRate), 0, static_cast<int>(size - 1));
      for (auto i = std::clamp(tmp, 0, static_cast<int>(size - 1)); i < std::clamp(tmp + 1000, 0, static_cast<int>(size - 2)); ++i)
        if (data[i] <= 0 && data[i + 1] >= 0)
          return i;
      return tmp;
    }();
    audio->unlock();
  }
}

auto App::getTex(double start) -> GLuint
{
  ImGuiIO &io = ImGui::GetIO();
  const auto Width = io.DisplaySize.x;
  if (!spec)
  {
    static Texture nullTexture = []() {
      Texture nullTexture;
      static std::vector<std::array<unsigned char, 3>> data;
      data.resize(16);

      glBindTexture(GL_TEXTURE_1D, nullTexture.get());
      glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexImage1D(GL_TEXTURE_1D,    // target
                   0,                // level
                   3,                // internalFormat
                   data.size(),      // width
                   0,                // border
                   GL_RGB,           // format
                   GL_UNSIGNED_BYTE, // type
                   data.data()       // data
      );
      return nullTexture;
    }();
    return nullTexture.get();
  }
  if (!specCache)
    specCache = std::make_unique<SpecCache>(*spec, k, Width, rangeTime, sampleRate);
  return specCache->getTex(start);
}

auto App::mouseButton(int x, int /*y*/, uint32_t state, uint8_t button) -> void
{
  // set the cursor on left mouse button

  if (button == SDL_BUTTON_LEFT)
  {
    if (state == SDL_PRESSED)
    {
      if (size < 2)
        return;
      ImGuiIO &io = ImGui::GetIO();
      (void)io;
      followMode = false;

      const auto Width = io.DisplaySize.x;
      if (!audio)
        return;
      audio->lock();
      cursor = [&]() {
        const auto tmp = std::clamp(static_cast<int>(x * rangeTime * sampleRate / Width + startTime * sampleRate), 0, static_cast<int>(size - 1));
        for (auto i = std::clamp(tmp, 0, static_cast<int>(size - 1)); i < std::clamp(tmp + 1000, 0, static_cast<int>(size - 2)); ++i)
          if (data[i] <= 0 && data[i + 1] >= 0)
            return i;
        return tmp;
      }();
      audio->unlock();
    }
  }
}

auto App::togglePlay() -> void
{
  if (!audio)
    return;
  if (isAudioPlaying)
    audio->pause(true);
  else
    audio->pause(false);
  isAudioPlaying = !isAudioPlaying;
}

auto App::cursorLeft() -> void
{
  if (size < 2)
    return;
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  followMode = false;
  const auto Width = io.DisplaySize.x;
  if (!audio)
    return;
  audio->lock();
  cursor = std::clamp(static_cast<int>(cursor - 4 * rangeTime / Width * sampleRate), 0, static_cast<int>(size - 1));
  audio->unlock();
}

auto App::cursorRight() -> void
{
  if (size < 2)
    return;
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  followMode = false;
  const auto Width = io.DisplaySize.x;
  if (!audio)
    return;
  audio->lock();
  cursor = std::clamp(static_cast<int>(cursor + 4 * rangeTime / Width * sampleRate), 0, static_cast<int>(size - 1));
  audio->unlock();
}
