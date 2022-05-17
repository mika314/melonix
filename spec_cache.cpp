#include "spec_cache.hpp"
#include <algorithm>
#include <cmath>

SpecCache::SpecCache(Spec &spec, float k, int screenWidth, double rangeTime, int sampleRate)
  : spec(spec), k(k), width(screenWidth), rangeTime(rangeTime), sampleRate(sampleRate)
{
}

auto SpecCache::getTex(double start) -> GLuint
{
  const auto key = static_cast<int>(start * width / rangeTime);
  {
    const auto it = range2Tex.find(key);
    if (it != std::end(range2Tex))
    {
      age.erase(it->second.age);
      age.push_front(key);
      it->second.age = std::begin(age);

      return populateTex(it->second.texture, it->second.isDirty, key);
    }
  }

  {
    if (range2Tex.size() < MaxRanges)
    {
      age.push_front(key);
      auto tmp = range2Tex.insert(std::make_pair(key, Tex{std::begin(age)}));
      auto retIt = tmp.first;
      return populateTex(retIt->second.texture.get(), retIt->second.isDirty, key);
    }
    // recycle textures
    // get the oldest texture
    auto oldest = std::end(age);
    --oldest;

    age.erase(oldest);

    const auto oldestKey = *oldest;
    const auto retIt = range2Tex.find(oldestKey);
    auto ret = std::move(retIt->second.texture);
    range2Tex.erase(retIt);

    age.push_front(key);
    auto tmp = range2Tex.insert(std::make_pair(key, Tex{std::move(ret), std::begin(age)}));

    return populateTex(tmp.first->second.texture.get(), tmp.first->second.isDirty, key);
  }
}

auto SpecCache::populateTex(GLuint texture, bool &isDirty, int key) -> GLuint
{
  glBindTexture(GL_TEXTURE_1D, texture);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

  if (!isDirty)
  {
    return texture;
  }

  const auto start = key * rangeTime / width;
  const auto pixelSize = std::max(4. / sampleRate, rangeTime / width);
  const auto s = spec.get().getSpec(static_cast<int>(start * sampleRate), static_cast<int>((start + pixelSize) * sampleRate));

  if (s.empty())
  {
    data.clear();
    for (auto i = 0; i < 16; ++i)
      data.push_back({static_cast<unsigned char>(0), static_cast<unsigned char>(0), static_cast<unsigned char>(0)});
  }
  else
  {
    isDirty = false;
    data.resize(s.size());
    for (auto i = 0U; i < s.size(); ++i)
    {
      const auto tmp = std::clamp(s[i] * k, 0.f, 255.f);
      if (tmp < 255 / 3)
      {
        data[i] = {static_cast<unsigned char>(tmp), 0, 0};
      }
      else if (tmp < 2 * 255 / 3)
      {
        const auto a = (tmp - 255 / 3) / (255 / 3) * 3.141592 / 2;
        const auto r = static_cast<unsigned char>(tmp * std::cos(a));
        const auto g = static_cast<unsigned char>(tmp * std::sin(a));
        data[i] = std::array<unsigned char, 3>{r, g, 0};
      }
      else
      {
        const auto k = static_cast<unsigned char>((tmp - 2 * 255 / 3) * 3);
        data[i] = std::array<unsigned char, 3>{k, static_cast<unsigned char>(tmp), k};
      }
    }
  }

  glTexImage1D(GL_TEXTURE_1D,    // target
               0,                // level
               3,                // internalFormat
               data.size(),      // width
               0,                // border
               GL_RGB,           // format
               GL_UNSIGNED_BYTE, // type
               data.data()       // data
  );

  return texture;
}
