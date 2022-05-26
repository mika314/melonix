#pragma once
#include <ser/macro.hpp>

struct Marker
{
  int sample;
  double note;
  double dTime;
  double pitchBend;

#define SER_PROP_LIST \
  SER_PROP(sample);   \
  SER_PROP(note);     \
  SER_PROP(dTime);    \
  SER_PROP(pitchBend);

  SER_DEF_PROPS();
#undef SER_PROP_LIST
};
