#pragma once
#include "file_open.hpp"

class App
{
public:
  auto draw() -> void;
  auto glDraw() -> void;
  auto mouseMotion(int x, int y, int dx, int dy, uint32_t state) -> void;

private:
  FileOpen fileOpen;
  float *data = nullptr;
  size_t size = 0;
  int sampleRate = 0;
  std::vector<std::vector<std::pair<float, float>>> picks;
  double startTime = 0.;
  double endTime = 10.;
  std::vector<std::pair<float, float>> waveformCache;

  auto load(const std::string &) -> void;
  auto calcPicks() -> void;
  auto getMinMaxFromRange(int start, int end) -> std::pair<float, float>;
};
