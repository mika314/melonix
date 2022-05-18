#pragma once
#include "file_open.hpp"
#include "marker.hpp"
#include "range.hpp"
#include "spec.hpp"
#include "spec_cache.hpp"
#include <imgui/imgui.h>
#include <list>
#include <sdlpp/sdlpp.hpp>
#include <unordered_map>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

class App
{
public:
  auto draw() -> void;
  auto glDraw() -> void;
  auto mouseMotion(int x, int y, int dx, int dy, uint32_t state) -> void;
  auto mouseButton(int x, int y, uint32_t state, uint8_t button) -> void;
  auto togglePlay() -> void;
  auto cursorLeft() -> void;
  auto cursorRight() -> void;
  auto loadFile(const std::string &) -> void;

private:
  FileOpen fileOpen;
  float *data = nullptr;
  size_t size = 0;
  int sampleRate = 0;
  std::vector<std::vector<std::pair<float, float>>> picks;
  double startTime = 0.;
  double rangeTime = 10.;
  double startNote = 24;
  double rangeNote = 60;
  std::vector<std::pair<float, float>> waveformCache;
  int cursor = 0;
  bool isAudioPlaying = false;
  bool followMode = false;

  std::unique_ptr<Spec> spec;
  float brightness = 50.f;
  float k = 0.01f;
  std::unique_ptr<sdl::Audio> audio;
  std::unique_ptr<SpecCache> specCache;
  double displayCursor;
  Texture pianoTexture;
  std::vector<Marker> markers;
  std::vector<Marker>::iterator movingMarker;

  auto calcPicks() -> void;
  auto getMinMaxFromRange(int start, int end) -> std::pair<float, float>;
  auto getTex(double start) -> GLuint;
  auto load(const std::string &) -> void;
};
