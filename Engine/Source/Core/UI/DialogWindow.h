#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>


#include "Core/Base.h"

namespace NeonEngine
{

enum class DialogType
{
    OpenFile,
    SaveFile,
    SelectFolder
};

class DialogWindow
{
  public:
    virtual ~DialogWindow() = default;

    // Open a dialog window and return the selected file/folder path
    // Returns empty optional if dialog was cancelled
    virtual std::optional<std::string> showDialog(
        DialogType                                              type,
        const std::string                                      &title,
        const std::vector<std::pair<std::string, std::string>> &filters = {}) = 0;

    // Create platform-specific dialog instance
    static std::unique_ptr<DialogWindow> create();
};

} // namespace NeonEngine
