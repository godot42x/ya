#pragma once

#include "App/Module/Module.h"

#include <memory>

namespace ya
{

struct EditorLayer;
struct Scene;

/// GameEditor IRuntimeModule. Per-frame GUI drive is the four hooks in
/// `EditorModule.cpp`; the file-level comment there is the full chain map.
[[nodiscard]] std::unique_ptr<IModule> createEditorModule();
[[nodiscard]] EditorLayer* getEditorLayer();
[[nodiscard]] Scene* getEditorAuthoringScene();

} // namespace ya

