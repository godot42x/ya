#pragma once

#include "GameEditor/FileExplorer.h"

#include "Core/Config/ConfigManager.h"
#include "Core/System/PathUtils.h"
#include "Core/System/VirtualFileSystem.h"
#include "GameEditor/ImGui/ImGuiHelper.h"

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
