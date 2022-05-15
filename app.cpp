#include "app.hpp"
#include <SDL.h>
#include <functional>
#include <imgui/imgui.h>
#include <log/log.hpp>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

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
      if (ImGui::MenuItem("Open", "Ctrl+O"))
      {
        LOG("Open");
        postponedAction = [&]() { ImGui::OpenPopup("FileOpen"); };
      }
      if (ImGui::MenuItem("Save"), "Ctrl+S") {}
      if (ImGui::MenuItem("Quit"), "Ctrl+Q") {}
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
  if (postponedAction)
    postponedAction();

  if (fileOpen.draw())
  {
    LOG("open", fileOpen.getSelectedFile());
    load(fileOpen.getSelectedFile());
    calcPicks();
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
  ImGuiIO &io = ImGui::GetIO();
  (void)io;

  const auto Height = io.DisplaySize.y;
  const auto Width = io.DisplaySize.x;

  glViewport(0, 0, (int)io.DisplaySize.x, static_cast<int>(Height - 20 - Height * .9));
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, Width, 2.f, -2.f, -1, 1);
  // Our state
  ImVec4 clear_color = ImVec4(0.45f, 0.0f, 0.30f, 1.00f);
  glClearColor(clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w);
  glClear(GL_COLOR_BUFFER_BIT);
  glColor3f(1.0f, 1.0f, 1.0f);

  if (waveformCache.size() != Width)
    waveformCache.clear();

  if (waveformCache.empty())
  {
    for (auto x = 0; x < Width; ++x)
    {
      const auto left = (1. * x / Width * (endTime - startTime) + startTime) * sampleRate;
      const auto right = (1. * (x + 1) / Width * (endTime - startTime) + startTime) * sampleRate;
      auto minMax = getMinMaxFromRange(left, right);
      waveformCache.push_back(minMax);
    }
  }

  // draw waveform
  glBegin(GL_LINE_STRIP);
  for (auto x = 0; x < Width; ++x)
  {
    const auto minMax = waveformCache[x];
    glVertex2f(x, minMax.first);
    glVertex2f(x + 1, minMax.second);
  }
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
  if (state & SDL_BUTTON_RMASK)
  {
    ImGuiIO &io = ImGui::GetIO();
    const auto Width = io.DisplaySize.x;

    auto modState = SDL_GetModState();
    const auto leftLimit = std::max(-(endTime - startTime) * 0.5, -.5 * size / sampleRate);
    const auto rightLimit = std::min(size / sampleRate + (endTime - startTime) * 0.5, 1.5 * size / sampleRate);
    if ((modState & (KMOD_LCTRL | KMOD_RCTRL)) == 0)
    {
      // panning
      const auto dt = 1. * dx / Width * (endTime - startTime);
      const auto newStartTime = startTime - dt;

      if (newStartTime < leftLimit)
        return;
      if (newStartTime > rightLimit)
        return;
      const auto newEndTime = endTime - dt;
      if (newEndTime < leftLimit)
        return;
      if (newEndTime > rightLimit)
        return;
      startTime = newStartTime;
      endTime = newEndTime;
      waveformCache.clear();
    }
    else
    {
      // zoom in or zoom out
      const auto zoom = 1. + 0.01 * dy;
      const auto cursorPos = 1. * x / Width * (endTime - startTime) + startTime;
      const auto newStartTime = (startTime - cursorPos) * zoom + cursorPos;
      const auto newEndTime = (endTime - cursorPos) * zoom + cursorPos;
      startTime = (newStartTime >= leftLimit && newStartTime <= rightLimit) ? newStartTime : startTime;
      endTime = (newEndTime >= leftLimit && newEndTime <= rightLimit) ? newEndTime : endTime;
      waveformCache.clear();
    }
  }
}
