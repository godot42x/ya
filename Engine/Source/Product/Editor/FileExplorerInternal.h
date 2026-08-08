#pragma once

#include "Product/Editor/FileExplorer.h"

#include "Product/Host/Config/ConfigManager.h"
#include "Foundation/Core/System/PathUtils.h"
#include "Foundation/Core/System/VirtualFileSystem.h"
#include "Product/Editor/ImGui/ImGuiHelper.h"

#include <algorithm>
#include <format>

namespace ya
{

inline std::string makeFileExplorerConfigKey(const FileExplorer& explorer, std::string_view suffix)
{
    std::string key = std::format("fileExplorer.{}", explorer.getConfigScope());
    if (!suffix.empty()) {
        key.push_back('.');
        key += suffix;
    }
    return key;
}

} // namespace ya
