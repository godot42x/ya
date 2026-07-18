#pragma once

#include "Runtime/App/IAppExtension.h"

#include <memory>

namespace ya
{

struct EditorLayer;

[[nodiscard]] std::unique_ptr<IAppExtension> createEditorAppExtension();
[[nodiscard]] EditorLayer* getEditorLayer();

} // namespace ya
