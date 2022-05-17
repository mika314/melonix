#pragma once
#include "spec.hpp"
#include "texture.hpp"
#include <imgui/imgui.h>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

class SpecCache
{
public:
  SpecCache(Spec &, float k, int screenWidth, double rangeTime, int sampleRate);
  auto getTex(double time) -> GLuint;

private:
  std::reference_wrapper<Spec> spec;
  float k;
  int width;
  double rangeTime;
  int sampleRate;
  struct Tex
  {
    explicit Tex(std::list<int>::iterator age) : age(std::move(age)) {}
    explicit Tex(Texture &&texture, std::list<int>::iterator age) : texture(std::move(texture)), age(std::move(age)) {}
    Texture texture;
    std::list<int>::iterator age;
    bool isDirty = true;
  };
  std::unordered_map<int, Tex> range2Tex;
  std::list<int> age;
  std::vector<std::array<unsigned char, 3>> data;

  auto populateTex(GLuint texture, bool &isDirty, int key) -> GLuint;
};
