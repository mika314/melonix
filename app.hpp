#pragma once
#include "file_open.hpp"

class App
{
public:
  auto draw() -> void;

private:
  FileOpen fileOpen;
};
