#pragma once
#include "file_open.hpp"
#include "range.hpp"
#include "spec.hpp"
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

private:
  FileOpen fileOpen;
  float *data = nullptr;
  size_t size = 0;
  int sampleRate = 0;
  std::vector<std::vector<std::pair<float, float>>> picks;
  double startTime = 0.;
  double endTime = 10.;
  std::vector<std::pair<float, float>> waveformCache;
  std::vector<GLuint> textures;
  int cursor = 0;
  bool isAudioPlaying = false;
  bool followMode = false;

  struct Tex
  {
    GLuint texture;
    std::list<Range>::iterator age;
    bool isDirty = true;
  };
  std::unordered_map<Range, Tex, pair_hash> range2Tex;
  std::list<Range> age;
  std::unique_ptr<Spec> spec;
  float brightness = 50.f;
  float k = 0.01f;
  std::unique_ptr<sdl::Audio> audio;

  auto calcPicks() -> void;
  auto getMinMaxFromRange(int start, int end) -> std::pair<float, float>;
  auto getTex(int start, int end) -> GLuint;
  auto load(const std::string &) -> void;
  auto populateTex(GLuint texture, bool &isDirty, int start, int end) -> GLuint;
};
