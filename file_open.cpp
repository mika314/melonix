#include "file_open.hpp"
#include <functional>
#include <imgui/imgui.h>
#include <log/log.hpp>

auto FileOpen::draw() -> bool
{
  if (!ImGui::BeginPopupModal("FileOpen", NULL, ImGuiWindowFlags_AlwaysAutoResize))
    return false;

  auto ret = false;

  if (files.empty())
  {
    // list files and directories in the current directory
    const auto cwd = std::filesystem::current_path();
    for (auto &entry : std::filesystem::directory_iterator(cwd))
      files.push_back(entry.path());
  }

  // Populate the files in the list box
  if (ImGui::BeginListBox("##files", ImVec2(700, 400)))
  {
    std::function<void()> postponedAction = nullptr;
    if (ImGui::Selectable("..", ".." == selectedFile, ImGuiSelectableFlags_AllowDoubleClick))
      if (ImGui::IsMouseDoubleClicked(0))
        postponedAction = [this]() {
          files.clear();
          selectedFile = "";
          // Get the current working directory.
          const auto cwd = std::filesystem::current_path();
          // Go up one directory.
          std::filesystem::current_path(cwd.parent_path());
        };

    for (auto &file : files)
    {
      const auto isHidden = file.filename().string().front() == '.';
      if (isHidden)
        continue;
      const auto isDirectory = std::filesystem::is_directory(file);

      if (ImGui::Selectable(
            ((isDirectory ? "> " : "  ") + file.filename().string()).c_str(), selectedFile == file, ImGuiSelectableFlags_AllowDoubleClick))
      {
        if (ImGui::IsMouseDoubleClicked(0))
        {
          if (isDirectory)
          {
            // change directory
            postponedAction = [this, file]() {
              std::filesystem::current_path(file);
              files.clear();
              selectedFile = "";
            };
          }
          else
          {
            selectedFile = file;
            ImGui::CloseCurrentPopup();
            ret = true;
          }
        }
        else
        {
          selectedFile = file;
        }
      }
    }
    if (postponedAction)
      postponedAction();
    ImGui::EndListBox();
  }

  // Show the selected file
  if (!selectedFile.empty())
    ImGui::Text("%s", selectedFile.filename().c_str());
  else
    ImGui::Text("No file selected");

  ImGui::SameLine(700 - 2 * 120 - 10);
  if (ImGui::Button("OK", ImVec2(120, 0)))
  {
    ImGui::CloseCurrentPopup();
    ret = true;
  }
  ImGui::SetItemDefaultFocus();
  ImGui::SameLine(700 - 120);
  if (ImGui::Button("Cancel", ImVec2(120, 0)))
    ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
  return ret;
}

auto FileOpen::getSelectedFile() const -> std::filesystem::path
{
  return selectedFile;
}
