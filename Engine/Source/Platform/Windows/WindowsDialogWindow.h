#pragma once

#include "Core/UI/DialogWindow.h"

namespace NeonEngine
{

class WindowsDialogWindow : public DialogWindow
{
  public:
    std::optional<std::string> showDialog(DialogType                                              type,
                                          const std::string                                      &title,
                                          const std::vector<std::pair<std::string, std::string>> &filters = {}) override;
};

} // namespace NeonEngine
