#include "app.hpp"
#include <functional>
#include <imgui/imgui.h>
#include <log/log.hpp>

auto App::draw() -> void
{
  std::function<void(void)> postponedAction = nullptr;

  if (ImGui::BeginMainMenuBar())
  {
    if (ImGui::BeginMenu("File"))
    {
      if (ImGui::MenuItem("Open", "Ctrl+O"))
      {
        LOG("Open");
        postponedAction = [&]() { ImGui::OpenPopup("FileOpen"); };
      }
      if (ImGui::MenuItem("Save"), "Ctrl+S") {}
      if (ImGui::MenuItem("Quit"), "Ctrl+Q") {}
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
  if (postponedAction)
    postponedAction();

  if (fileOpen.draw())
  {
    LOG("open", fileOpen.getSelectedFile());
  }
}
