#pragma once
#include "file_open.hpp"

class App
{
public:
  auto draw() -> void;

private:
  auto load(const std::string &) -> void;
  FileOpen fileOpen;
  double *data = nullptr;
  size_t size = 0;
  int sampleRate = 0;
};
