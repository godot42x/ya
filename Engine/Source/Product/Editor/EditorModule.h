#pragma once

#include "Core/Module/Module.h"

#include <memory>

namespace ya
{

struct EditorLayer;
struct Scene;

[[nodiscard]] std::unique_ptr<IModule> createEditorModule();
[[nodiscard]] EditorLayer* getEditorLayer();
[[nodiscard]] Scene* getEditorAuthoringScene();

} // namespace ya

