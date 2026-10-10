#include "ECS/Systems/ScriptInstance.h"

#include "ECS/Systems/Components/LuaScriptComponent.h"
#include "ECS/Systems/ScriptPropertyPreview.h"
#include "ECS/Systems/UILogicAuthoring.h"
#include "Resource/AssetManager.h"

namespace ya
{
namespace
{

void (*gDropScriptInstance)(uint64_t) = nullptr;

IScriptPropertyPreview* gScriptPropertyPreview = nullptr;
IUILogicAuthoring*      gUILogicAuthoring      = nullptr;

} // namespace

void setDropScriptInstance(void (*fn)(uint64_t runtimeId))
{
    gDropScriptInstance = fn;
}

void dropScriptInstance(uint64_t runtimeId)
{
    if (runtimeId == 0 || !gDropScriptInstance) {
        return;
    }
    gDropScriptInstance(runtimeId);
}

void setScriptPropertyPreview(IScriptPropertyPreview* preview)
{
    gScriptPropertyPreview = preview;
}

IScriptPropertyPreview* scriptPropertyPreview()
{
    return gScriptPropertyPreview;
}

void setUILogicAuthoring(IUILogicAuthoring* authoring)
{
    gUILogicAuthoring = authoring;
}

IUILogicAuthoring* uiLogicAuthoring()
{
    return gUILogicAuthoring;
}

std::string ScriptInstance::normalizeScriptPath(std::string_view path)
{
    return AssetManager::normalizeScriptAssetPath(path);
}

void LuaScriptComponent::releaseLuaHandles()
{
    for (auto& script : scripts) {
        if (script.runtimeId == 0) {
            continue;
        }
        dropScriptInstance(script.runtimeId);
        script.bLoaded   = false;
        script.runtimeId = 0;
    }
}

} // namespace ya
