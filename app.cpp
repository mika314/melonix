#include "app.hpp"
#include <functional>
#include <imgui/imgui.h>
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
  }
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
  av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_DBL, 0);
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
    double *buffer;
    av_samples_alloc((uint8_t **)&buffer, NULL, 1, frame->nb_samples, AV_SAMPLE_FMT_DBL, 0);
    int frame_count = swr_convert(swr, (uint8_t **)&buffer, frame->nb_samples, (const uint8_t **)frame->data, frame->nb_samples);
    // append resampled frames to data
    data = (double *)realloc(data, (size + frame->nb_samples) * sizeof(double));
    memcpy(data + size, buffer, frame_count * sizeof(double));
    size += frame_count;
  }

  // clean up
  av_packet_unref(packet);
  av_frame_free(&frame);
  swr_free(&swr);
  avcodec_close(codec);
  avformat_free_context(format);

  LOG("File loaded", path, "duration", 1. * size / sampleRate, "smaple rate", sampleRate);
}
