#pragma once
#include <array>
#include <filesystem>
#include <vector>

class FileSaveAs
{

private:
  std::vector<std::filesystem::path> files;
  std::filesystem::path selectedFile;
  std::array<char, 255> fileName;

public:
  FileSaveAs(std::string);
  auto draw() -> bool;
  auto getSelectedFile() const -> std::string;

  std::string dialogName;
};
