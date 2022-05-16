#include "spec.hpp"
#include <cassert>
#include <cmath>
#include <cstring>
#include <log/log.hpp>
#include <optional>

const auto SpectrSize = 8 * 4096;

Spec::Spec(std::span<float> wav)
  : wav(wav), input(fftw_alloc_complex(SpectrSize)), output(fftw_alloc_complex(SpectrSize)), running(true), thread(std::thread(&Spec::run, this))
{
  memset(input, 0, SpectrSize * sizeof(fftw_complex));
  memset(output, 0, SpectrSize * sizeof(fftw_complex));
  plan = fftw_plan_dft_1d(SpectrSize, input, output, FFTW_FORWARD, FFTW_MEASURE);
}

auto Spec::getSpec(int start, int end) const -> std::vector<float>
{
  const auto key = std::make_pair(start, end);
  std::lock_guard<std::mutex> lock(mutex);
  auto it = range2Spec.find(key);
  if (it != std::end(range2Spec))
  {
    age.erase(it->second.age);
    age.push_front(key);
    it->second.age = std::begin(age);
    return it->second.spec;
  }
  jobs.insert(key);
  age.push_front(key);
  range2Spec.insert(std::make_pair(key, S{{}, std::begin(age)}));
  if (range2Spec.size() > MaxRanges)
  {
    auto oldest = std::end(age);
    --oldest;
    range2Spec.erase(*oldest);
    jobs.erase(*oldest);
    age.pop_back();
  }
  return {};
}

auto Spec::internalGetSpec(int start, int end) const -> std::vector<float>
{
  auto p = 0;
  for (auto i = end - SpectrSize; i < end; ++i)
  {
    input[p][1] = 0;
    if ((i >= static_cast<int>(wav.size()) || i < 0))
      input[p][0] = 0;
    else
    {
      if (i >= start)
        input[p][0] = wav[i];
      else
        input[p][0] = expf(-0.00025f * (start - i)) * wav[i];
    }
    ++p;
  }
  fftw_execute(plan);
  std::vector<float> ret;
  for (auto i = 0U; i < SpectrSize / 2; i++)
    ret.push_back(sqrt(output[i][0] * output[i][0] + output[i][1] * output[i][1]) / SpectrSize);
  return ret;
}

auto Spec::run() -> void
{
  while (running)
  {
    const auto job = [&]() -> std::optional<Range> {
      std::lock_guard<std::mutex> lock(mutex);
      if (jobs.empty())
        return std::nullopt;
      auto key = *jobs.begin();
      jobs.erase(key);
      return key;
    }();

    if (!job)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      continue;
    }

    auto spec = internalGetSpec(job->first, job->second);

    {
      std::lock_guard<std::mutex> lock(mutex);
      auto it = range2Spec.find(*job);
      if (it == std::end(range2Spec))
        continue;
      it->second.spec = spec;
    }
  }
}

Spec::~Spec()
{
  running = false;
  thread.join();
  fftw_destroy_plan(plan);
  fftw_free(input);
  fftw_free(output);
}
