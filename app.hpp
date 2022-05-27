#pragma once
#include "file-open.hpp"
#include "file-save-as.hpp"
#include "marker.hpp"
#include "range.hpp"
#include "spec.hpp"
#include "spec-cache.hpp"
#include <imgui/imgui.h>
#include <list>
#include <map>
#include <sdlpp/sdlpp.hpp>
#include <ser/macro.hpp>
#include <unordered_map>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

class App
{
public:
  App();
  auto draw() -> void;
  auto glDraw() -> void;
  auto mouseMotion(int x, int y, int dx, int dy, uint32_t state) -> void;
  auto mouseButton(int x, int y, uint32_t state, uint8_t button) -> void;
  auto togglePlay() -> void;
  auto cursorLeft() -> void;
  auto cursorRight() -> void;
  auto openFile(const std::string &) -> void;

private:
  const int version = 1;
  FileOpen fileOpen;
  FileSaveAs fileSaveAs;
  FileSaveAs exportWavDlg;
  std::vector<float> wavData;
  std::map<int, std::tuple<std::span<float>, int>> grains;
  int sampleRate = 0;
  std::vector<std::vector<std::pair<float, float>>> picks;
  double startTime = 0.;
  double rangeTime = 10.;
  double startNote = 24.;
  double rangeNote = 60.;
  mutable std::vector<std::pair<float, float>> waveformCache;
  double cursorSec = 0.0;
  bool isAudioPlaying = false;
  bool followMode = false;

  std::unique_ptr<Spec> spec;
  float brightness = 50.f;
  float k = 0.01f;
  std::unique_ptr<sdl::Audio> audio;
  mutable std::unique_ptr<SpecCache> specCache;
  double displayCursor;
  Texture pianoTexture;
  std::vector<Marker> markers;
  std::vector<Marker>::iterator selectedMarker;
  mutable std::unordered_map<int, double> sample2TimeCache;
  mutable std::unordered_map<int, int> time2SampleCache;
  mutable std::unordered_map<int, double> time2PitchBendCache;
  float tempo = 130.f;
  std::string saveName;
  double bias = 0.;
  std::vector<float> restWav;
  std::span<float> prevGrain;

public:
#define SER_PROP_LIST   \
  SER_PROP(wavData);    \
  SER_PROP(sampleRate); \
  SER_PROP(brightness); \
  SER_PROP(markers);    \
  SER_PROP(tempo);
  SER_DEF_PROPS();
#undef SER_PROP_LIST
private:
  auto calcPicks() -> void;
  auto cleanup() -> void;
  auto drawMarkers() -> void;
  auto duration() const -> double;
  auto estimateGrainSize(int start) const -> int;
  auto exportWav(const std::string &) -> void;
  auto getMinMaxFromRange(int start, int end) -> std::pair<float, float>;
  auto getTex(double start) -> GLuint;
  auto importFile(const std::string &) -> void;
  auto invalidateCache() const -> void;
  auto loadAudioFile(const std::string &) -> void;
  auto loadMelonixFile(const std::string &) -> void;
  auto playback(float *, size_t) -> void;
  auto preproc() -> void;
  auto process(double cursor, std::vector<float> &wav) -> double;
  auto sample2Time(int) const -> double;
  auto saveMelonixFile(std::string) -> void;
  auto time2PitchBend(double) const -> double;
  auto time2Sample(double) const -> int;
};
