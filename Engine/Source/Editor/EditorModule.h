#pragma once

#include "Core/Module/Module.h"

#include <memory>

namespace ya
{

struct EditorLayer;

[[nodiscard]] std::unique_ptr<IModule> createEditorModule();
[[nodiscard]] EditorLayer* getEditorLayer();

} // namespace ya
