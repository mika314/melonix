#pragma once
#include <imgui/imgui.h>

#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <SDL_opengles2.h>
#else
#include <SDL_opengl.h>
#endif

class Texture
{
public:
  Texture() { glGenTextures(1, &name); }
  ~Texture() { glDeleteTextures(1, &name); }
  // disable copy
  Texture(const Texture &) = delete;
  Texture &operator=(const Texture &) = delete;
  // moving
  Texture(Texture &&other)
  {
    name = other.name;
    other.name = 0;
  }
  Texture &operator=(Texture &&other)
  {
    name = other.name;
    other.name = 0;
    return *this;
  }

  auto get() const -> GLuint { return name; }
  operator GLuint() const { return name; }

private:
  GLuint name;
};
