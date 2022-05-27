#pragma once
#include <vector>
#include <string>

auto saveWav(const std::string &fileName, const std::vector<int16_t> &wav, int sampleRate) -> void;
