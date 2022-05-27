#pragma once
#include <filesystem>
#include <vector>

class FileOpen
{

private:
  std::vector<std::filesystem::path> files;
  std::filesystem::path selectedFile;

public:
  auto draw() -> bool;
  auto getSelectedFile() const -> std::filesystem::path;
};
