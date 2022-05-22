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
    loadFile(fileOpen.getSelectedFile());

  {
    ImGui::Begin("Control Center");
    ImGui::Text("<%.2f %.2f %.2f>", startTime, cursorSec, startTime + rangeTime);
    ImGui::SameLine();
    ImGui::Text("<%.2f %.2f>", startNote, startNote + rangeNote);
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
    // Tempo
    ImGui::SliderFloat("Tempo", &tempo, 30.0f, 250.0f);
    const auto &io = ImGui::GetIO();
    ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
    ImGui::End();
  }
  if (audio)
  {
    audio->lock();
    displayCursor = cursorSec;
    audio->unlock();
    if (displayCursor > startTime + rangeTime && isAudioPlaying)
      followMode = true;
    if (followMode)
    {
      const auto desieredStart = displayCursor - rangeTime / 5;
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

static auto GrainSpectrSize = 2 * 4096;

auto App::loadFile(const std::string &fileName) -> void
{
  LOG("open", fileName);
  specCache = nullptr;
  spec = nullptr;
  audio = nullptr;
  startTime = 0.;
  rangeTime = 10.;
  cursorSec = 0;
  load(fileName);

  {
    // generate grains
    grains.clear();
    auto start = 0;
    auto grainSize = estimateGrainSize(start);
    auto nextEstimation = GrainSpectrSize;
    while (start < static_cast<int>(size - grainSize - 1))
    {
      bool found = false;
      for (auto i = 0; i < grainSize; ++i)
      {
        const auto idx = start + grainSize + (i % 2 == 0 ? i / 2 : -i / 2);
        const auto isZeroCrossing = data[idx] < 0 && data[idx + 1] >= 0;
        if (isZeroCrossing)
        {
          grains.insert(std::make_pair(start, std::span<float>(data + start, idx - start)));
          LOG("grain", start, idx - start);
          start = idx;
          found = true;
          break;
        }
      }
      if (!found)
      {
        LOG("bad grain", start, grainSize);
        found = false;
        for (auto i = start + grainSize + grainSize / 2; i < static_cast<int>(size - 1); ++i)
        {
          const auto isZeroCrossing = data[i] < 0 && data[i + 1] >= 0;
          if (isZeroCrossing)
          {
            grains.insert(std::make_pair(start, std::span<float>(data + start, i - start)));
            LOG("grain", start, i - start);
            start = i;
            found = true;
            break;
          }
        }
        if (!found)
          break;
      }
      if (start > nextEstimation)
      {
        nextEstimation += GrainSpectrSize;
        grainSize = estimateGrainSize(start);
        LOG("estimate", start, grainSize);
      }
    }
  }

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

    if (cursorSec < 0 || cursorSec >= duration())
      isAudioPlaying = false;

    if (!isAudioPlaying)
    {
      audio->pause(true);
      restWav.clear();
      return;
    }

    auto dur = len / static_cast<int>(sizeof(float));
    if (!restWav.empty())
    {
      auto sz = std::min(static_cast<int>(restWav.size()), dur);

      for (auto i = 0; i < sz; ++i)
      {
        *w = restWav[i];
        ++w;
      }

      dur -= sz;
      restWav.erase(restWav.begin(), restWav.begin() + sz);
      cursorSec += 1. * sz / sampleRate;
    }

    while (dur > 0)
    {
      const auto sample = time2Sample(cursorSec);
      const auto pitchBend = time2PitchBend(cursorSec);
      const auto rate = pow(2, pitchBend / 12);
      auto it = grains.lower_bound(sample);

      if (it == std::end(grains))
      {
        isAudioPlaying = false;
        return;
      }

      const auto grain = it->second;

      auto sz = 0;
      for (auto i = 0; i < dur; ++i)
      {
        const auto idx = static_cast<size_t>(i * rate + bias);
        if (idx >= grain.size())
          break;
        *w = grain[idx];
        ++w;
        ++sz;
      }
      cursorSec += 1. * sz / sampleRate;
      // copy rest to the rest wav
      restWav.clear();
      for (auto i = sz;; ++i)
      {
        const auto idx = static_cast<size_t>(i * rate + bias);
        if (idx >= grain.size())
        {
          bias = std::fmod(i * rate + bias, 1.f);
          break;
        }
        restWav.push_back(grain[idx]);
      }
      dur -= sz;
    }
  });
  spec = std::make_unique<Spec>(std::span<float>{data, data + size});
  markers.clear();
  movingMarker = std::end(markers);
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
  const auto startFreq = 55. * pow(2., (startNote - 24) / 12.);

  const auto &io = ImGui::GetIO();
  const auto Height = io.DisplaySize.y;
  const auto Width = io.DisplaySize.x;

  // Enable alpha blending
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  glViewport(0, 0, (int)io.DisplaySize.x, static_cast<int>(.1 * Height));
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, Width, 1.f, -1.f, -1, 1);
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
      const auto left = time2Sample(1. * x / Width * rangeTime + startTime);
      const auto right = time2Sample(1. * (x + 1) / Width * rangeTime + startTime);
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
  glViewport(0, static_cast<int>(.1 * Height), (int)io.DisplaySize.x, static_cast<int>(Height * 0.9 - 20));
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, Width, 0.f, 1.f, -1, 1);
  glEnable(GL_TEXTURE_1D);
  glColor3f(1.f, 1.f, 1.f);

  auto step = pow(2., 1. / 12.);

  for (auto x = 0; x < Width; ++x)
  {
    auto texture = getTex(startTime + x * rangeTime / Width);
    glBindTexture(GL_TEXTURE_1D, texture);

    glBegin(GL_QUADS);

    auto pitchBend = time2PitchBend(startTime + x * rangeTime / Width);
    auto freq = startFreq / sampleRate * 2.;
    for (auto i = 0; i < rangeNote; ++i)
    {
      glTexCoord1f(freq);
      glVertex2f(x, (i + pitchBend) / rangeNote);

      glTexCoord1f(freq * step);
      glVertex2f(x, 1.f * (i + pitchBend + 1.) / rangeNote);

      glTexCoord1f(freq * step);
      glVertex2f(x + 1.f, (i + pitchBend + 1.) / rangeNote);

      glTexCoord1f(freq);
      glVertex2f(x + 1.f, (i + pitchBend) / rangeNote);

      freq *= step;
    }
    glEnd();
  }
  // draw piano
  glColor4f(1.f, 1.f, 1.f, .096f);
  glBindTexture(GL_TEXTURE_1D, pianoTexture);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  std::vector<std::array<unsigned char, 3>> pianoData;
  pianoData.resize(.9 * Height - 20);
  auto lastNote = 0;
  for (auto i = 0U; i < pianoData.size(); ++i)
  {
    const auto tmp = i * rangeNote + pianoData.size() / 2;
    const auto note = static_cast<int>(tmp / pianoData.size() + startNote);
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

  // draw bars
  const auto beatDuration = 60. / tempo;
  glBegin(GL_LINES);
  for (auto x = static_cast<int>(startTime / beatDuration); x * beatDuration < startTime + rangeTime; ++x)
  {
    if (x % 4 == 0)
      glColor4f(1.f, 1.f, 1.f, .096f);
    else
      glColor4f(1.f, 1.f, 1.f, .04f);
    const auto pxX = (x * beatDuration - startTime) * Width / rangeTime;
    glVertex2f(pxX, 0);
    glVertex2f(pxX, 1);
  }
  glEnd();

  // draw markers
  glBegin(GL_LINES);
  for (const auto &marker : markers)
  {
    const auto x0 = (sample2Time(marker.sample) - startTime - marker.dTime) * Width / rangeTime;
    const auto y0 = (marker.note - startNote) / rangeNote;
    const auto x = (sample2Time(marker.sample) - startTime) * Width / rangeTime;
    const auto y = (marker.note - startNote + marker.pitchBend) / rangeNote;
    glColor3f(0.5f, 0.5f, 0.5f);
    glVertex2f(x0, y0);
    glVertex2f(x, y);

    glVertex2f(x0 - 2, y0 - 0.0025f);
    glVertex2f(x0 + 2, y0 + 0.0025f);
    glVertex2f(x0 + 2, y0 - 0.0025f);
    glVertex2f(x0 - 2, y0 + 0.0025f);

    glColor3f(0.f, 0.5f, 1.f);
    glVertex2f(x - 2, y - 0.0025f);
    glVertex2f(x + 2, y + 0.0025f);
    glVertex2f(x + 2, y - 0.0025f);
    glVertex2f(x - 2, y + 0.0025f);
  }
  glEnd();

  // draw a scrubber
  glViewport(0, 0, (int)io.DisplaySize.x, static_cast<int>(Height - 20));
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, Width, 0.f, 1.f, -1, 1);

  glColor4f(1.f, 0.f, 0.5f, 0.25f);
  glBegin(GL_LINES);
  glVertex2f((1. * displayCursor - startTime) / rangeTime * Width, 0.f);
  glVertex2f((1. * displayCursor - startTime) / rangeTime * Width, 1.0f);
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

  LOG("File loaded", path, "duration", 1. * size / sampleRate, "sample rate", sampleRate);
}

auto App::mouseMotion(int x, int y, int dx, int dy, uint32_t state) -> void
{
  if (size == 0)
    return;

  y -= 20;

  const auto &io = ImGui::GetIO();
  const auto Width = io.DisplaySize.x;
  const auto Height = io.DisplaySize.y * .9 - 20;
  if (state & SDL_BUTTON_MMASK)
  {
    auto modState = SDL_GetModState();
    const auto leftLimit = std::max(-rangeTime * 0.5, -.5 * size / sampleRate);
    const auto rightLimit = std::min(size / sampleRate + rangeTime * 0.5, 1.5 * size / sampleRate);
    if ((modState & (KMOD_LCTRL | KMOD_RCTRL)) != 0)
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
    else if ((modState & (KMOD_LALT | KMOD_RALT)) != 0)
    {
      {
        // y motion vertical panning
        const auto delta = 1. * dy * rangeNote / Height;
        auto newStartNote = startNote + delta;
        if (newStartNote < 0.)
          newStartNote = 0.;
        else if (newStartNote + rangeNote > 127.)
          newStartNote = 127. - rangeNote;
        startNote = newStartNote;
      }
      {
        // x motion zoom
        const auto zoom = 1. - 0.001 * dx;
        const auto cursorPos = 1. * (Height - y) / Height * rangeNote + startNote;
        const auto newStartNote = (startNote - cursorPos) * zoom + cursorPos;
        const auto newEndNote = (startNote + rangeNote - cursorPos) * zoom + cursorPos;
        startNote = (newStartNote >= 0. && newStartNote <= 127.) ? newStartNote : startNote;
        if (newEndNote >= 0. && newEndNote <= 127.)
          rangeNote = newEndNote - startNote;
        else if (newEndNote < 0.)
          rangeNote = 10.;
        else if (newEndNote > 127.)
          rangeNote = 127. - startNote;
      }
    }
    else
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
  }
  else if (state & SDL_BUTTON_LMASK)
  {
    if (movingMarker == std::end(markers))
    {
      if (y > Height)
      {
        if (!audio)
          return;
        audio->lock();
        cursorSec = std::clamp(x * rangeTime / Width + startTime, 0., duration());
        audio->unlock();
      }
    }
    else
    {
      const auto dX = dx * rangeTime / Width;
      const auto dY = dy * rangeNote / Height;
      movingMarker->dTime += dX;
      movingMarker->pitchBend -= dY;
      sample2TimeCache.clear();
      time2SampleCache.clear();
      time2PitchBendCache.clear();
      waveformCache.clear();
      if (specCache)
        specCache->clear();
    }
  }
}

auto App::getTex(double start) -> GLuint
{
  const auto &io = ImGui::GetIO();
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
    specCache = std::make_unique<SpecCache>(*spec, k, Width, rangeTime, [this](double val) { return time2Sample(val); });
  return specCache->getTex(start);
}

auto App::mouseButton(int x, int y, uint32_t state, uint8_t button) -> void
{
  y -= 20;
  const auto &io = ImGui::GetIO();
  const auto Width = io.DisplaySize.x;
  const auto Height = io.DisplaySize.y * .9 - 20;

  movingMarker = std::end(markers);
  std::sort(markers.begin(), markers.end(), [](const auto &a, const auto &b) { return a.sample < b.sample; });
  if (button == SDL_BUTTON_LEFT)
  {
    if (state != SDL_PRESSED)
      return;
    if (size < 2)
      return;

    if (y > Height)
    {
      // scrab
      followMode = false;
      if (!audio)
        return;
      audio->lock();
      cursorSec = std::clamp(x * rangeTime / Width + startTime, 0., duration());
      audio->unlock();
    }
    else
    {
      const auto time = x * rangeTime / Width + startTime;
      const auto sample = time2Sample(time);
      const auto note = (Height - y) * rangeNote / Height + startNote;
      const auto dTime = 8 * rangeTime / Width;
      const auto dNote = 8 * rangeNote / Height;

      auto it = std::find_if(std::begin(markers), std::end(markers), [dNote, dTime, note, this, time](const auto &m) {
        return std::abs(sample2Time(m.sample) - time) < dTime && std::abs(m.note - note + m.pitchBend) < dNote;
      });
      if (it == std::end(markers))
      {
        // add marker
        const auto pitchBend = time2PitchBend(time);
        markers.push_back(Marker{sample, note - pitchBend, 0., pitchBend});
        std::sort(markers.begin(), markers.end(), [](const auto &a, const auto &b) { return a.sample < b.sample; });
        sample2TimeCache.clear();
        time2SampleCache.clear();
        time2PitchBendCache.clear();
        waveformCache.clear();
        if (specCache)
          specCache->clear();

        movingMarker = std::end(markers);
      }
      else
      {
        // move marker
        LOG("Moving marker", it->sample, "dTime", it->dTime, "pitchBend", it->pitchBend);
        movingMarker = it;
      }
    }
  }
  else if (button == SDL_BUTTON_RIGHT)
  {
    if (state != SDL_PRESSED)
      return;
    // remove marker
    if (size < 2)
      return;
    const auto time = x * rangeTime / Width + startTime;
    const auto note = (Height - y) * rangeNote / Height + startNote;
    const auto dTime = 8 * rangeTime / Width;
    const auto dNote = 8 * rangeNote / Height;
    auto it = std::find_if(std::begin(markers), std::end(markers), [dNote, dTime, note, this, time](const auto &m) {
      return std::abs(sample2Time(m.sample) - time) < dTime && std::abs(m.note - note + m.pitchBend) < dNote;
    });
    if (it != std::end(markers))
    {
      markers.erase(it);
      sample2TimeCache.clear();
      time2SampleCache.clear();
      time2PitchBendCache.clear();
      waveformCache.clear();
      if (specCache)
        specCache->clear();
    }
  }
}

auto App::togglePlay() -> void
{
  if (!audio)
    return;
  isAudioPlaying = !isAudioPlaying;
  if (isAudioPlaying)
    audio->pause(false);
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
  cursorSec = std::clamp(cursorSec - 4 * rangeTime / Width, 0., duration());
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
  cursorSec = std::clamp(cursorSec + 4 * rangeTime / Width, 0., duration());
  audio->unlock();
}

auto App::sample2Time(int val) const -> double
{
  // markers are sorted by sample

  if (val <= 0)
    return 1. * val / sampleRate;

  const auto it = sample2TimeCache.find(val);
  if (it != std::end(sample2TimeCache))
    return it->second;

  auto prevSample = 0;
  auto prevTime = 0.0;
  for (const auto &marker : markers)
  {
    const auto rightTime = prevTime + 1.0 * (marker.sample - prevSample) / sampleRate + marker.dTime;
    if (val > prevSample && val <= marker.sample)
    {
      const auto ret = prevTime + (val - prevSample) * (rightTime - prevTime) / (marker.sample - prevSample);
      sample2TimeCache[val] = ret;
      return ret;
    }
    prevSample = marker.sample;
    prevTime = rightTime;
  }

  const auto ret = prevTime + 1. * (val - prevSample) / sampleRate;
  sample2TimeCache[val] = ret;
  return ret;
}

auto App::time2Sample(double val) const -> int
{
  // markers are sorted by sample

  if (val <= 0)
    return static_cast<int>(val * sampleRate);

  const auto it = time2SampleCache.find(static_cast<int>(val * sampleRate));
  if (it != std::end(time2SampleCache))
    return it->second;

  auto prevSample = 0;
  auto prevTime = 0.0;
  for (const auto &marker : markers)
  {
    const auto rightTime = prevTime + 1.0 * (marker.sample - prevSample) / sampleRate + marker.dTime;
    if (val > prevTime && val <= rightTime)
    {
      const auto ret = prevSample + (val - prevTime) * (marker.sample - prevSample) / (rightTime - prevTime);
      time2SampleCache[static_cast<int>(val * sampleRate)] = ret;
      return ret;
    }
    prevSample = marker.sample;
    prevTime = rightTime;
  }

  const auto ret = prevSample + (val - prevTime) * sampleRate;
  time2SampleCache[static_cast<int>(val * sampleRate)] = ret;
  return ret;
}

auto App::duration() const -> double
{
  return sample2Time(size - 1);
}

auto App::time2PitchBend(double val) const -> double
{
  if (val <= 0)
    return 0;
  const auto it = time2PitchBendCache.find(static_cast<int>(val * sampleRate));
  if (it != std::end(time2PitchBendCache))
    return it->second;

  auto prevSample = 0;
  auto prevTime = 0.0;
  auto prevPitchBend = 0.0;
  for (const auto &marker : markers)
  {
    const auto rightTime = prevTime + 1.0 * (marker.sample - prevSample) / sampleRate + marker.dTime;
    if (val > prevTime && val <= rightTime)
    {
      const auto ret = prevPitchBend + (val - prevTime) * (marker.pitchBend - prevPitchBend) / (rightTime - prevTime);
      time2PitchBendCache[static_cast<int>(val * sampleRate)] = ret;
      return ret;
    }
    prevSample = marker.sample;
    prevTime = rightTime;
    prevPitchBend = marker.pitchBend;
  }

  if (val > duration())
    return 0;

  const auto ret = prevPitchBend + (val - prevTime) * (0 - prevPitchBend) / (duration() - prevTime);
  time2PitchBendCache[static_cast<int>(val * sampleRate)] = ret;
  return ret;
}

namespace
{
  class GrainSpec
  {
  public:
    GrainSpec() : input(fftw_alloc_complex(GrainSpectrSize)), output(fftw_alloc_complex(GrainSpectrSize))
    {
      memset(input, 0, sizeof(fftw_complex) * GrainSpectrSize);
      memset(output, 0, sizeof(fftw_complex) * GrainSpectrSize);
      plan = fftw_plan_dft_1d(GrainSpectrSize, input, output, FFTW_FORWARD, FFTW_MEASURE);
    }

    ~GrainSpec()
    {
      fftw_destroy_plan(plan);
      fftw_free(input);
      fftw_free(output);
    }

    fftw_plan plan;
    fftw_complex *input;
    fftw_complex *output;
  };
} // namespace

auto App::estimateGrainSize(int start) const -> int
{
  static GrainSpec spec;
  for (auto i = 0; i < GrainSpectrSize; ++i)
  {
    spec.input[i][0] = data[std::min(start + i, static_cast<int>(size - 1))];
    spec.input[i][1] = 0;
  }
  fftw_execute(spec.plan);
  // find max value and index
  double max = 0;
  int maxIndex = 20 * GrainSpectrSize / sampleRate;
  for (auto i = maxIndex; i < GrainSpectrSize / 2 / 4; ++i)
  {
    const auto val = std::abs(spec.output[i][0]) + std::abs(spec.output[i][1]);
    if (val > max)
    {
      max = val;
      maxIndex = i;
    }
  }
  maxIndex = maxIndex * 4 - maxIndex / 4;
  max = 0;
  for (auto i = maxIndex; i < GrainSpectrSize / 2; ++i)
  {
    const auto val = std::abs(spec.output[i][0]) + std::abs(spec.output[i][1]);
    if (val > max)
    {
      max = val;
      maxIndex = i;
    }
  }

  const auto maxFreq = std::max(1., 1. * maxIndex * sampleRate / GrainSpectrSize / 4.);
  LOG("freq", maxFreq);

  return std::ceil(1500. * maxFreq / sampleRate) * sampleRate / maxFreq;
}
