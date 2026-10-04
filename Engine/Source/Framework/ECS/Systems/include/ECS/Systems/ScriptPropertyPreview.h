#pragma once

#include "Core/Api.h"

namespace ya
{

struct ScriptInstance;

/// Fills `instance.properties` from a script file and leaves no live Lua
/// handle on the instance. The Lua backend registers the implementation.
/// A null `scriptPropertyPreview()` means the inspector shows no property
/// rows and does not crash.
struct YA_ECS_SYSTEMS_API IScriptPropertyPreview
{
    virtual ~IScriptPropertyPreview() = default;

    virtual bool load(ScriptInstance& instance) = 0;
};

/// The pointer lives in ya-ecs-systems (one copy per process). Not an inline
/// static in this header: each DLL would otherwise keep its own.
YA_ECS_SYSTEMS_API void                       setScriptPropertyPreview(IScriptPropertyPreview* preview);
YA_ECS_SYSTEMS_API IScriptPropertyPreview*    scriptPropertyPreview();

} // namespace ya
