#pragma once
#include "range.hpp"
#include <deque>
#include <fftw3.h>
#include <list>
#include <span>
#include <thread>
#include <unordered_set>
#include <vector>

class Spec
{
public:
  Spec(std::span<float> wav);
  ~Spec();
  auto getSpec(int start, int end) const -> std::vector<float>;

private:
  std::span<float> wav;
  mutable fftw_plan plan;
  mutable fftw_complex *input;
  mutable fftw_complex *output;
  std::atomic<bool> running{false};
  mutable std::mutex mutex;
  mutable std::unordered_set<Range, pair_hash> jobs;
  std::thread thread;

  struct S
  {
    std::vector<float> spec;
    std::list<Range>::iterator age;
  };

  mutable std::unordered_map<Range, S, pair_hash> range2Spec;
  mutable std::list<Range> age;

  auto run() -> void;
  auto internalGetSpec(int start, int end) const -> std::vector<float>;
};
