#pragma once

#include "Core/Api.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

namespace ya
{

struct ScriptInstance;

/// Builds the one opaque behavior spec the designer writes onto a document
/// root. The Lua backend registers the implementation. A null
/// `uiLogicAuthoring()` means the designer shows no Logic row and does not
/// crash. The editor never names a script language: `type` and `data` come
/// from this interface, and the document stores them as a behavior.
struct YA_ECS_SYSTEMS_API IUILogicAuthoring
{
    virtual ~IUILogicAuthoring() = default;

    virtual void describe(std::string path, std::string& type, nlohmann::json& data) const = 0;

    /// True when `type` is a spec this authoring owns. An empty path is still ours.
    [[nodiscard]] virtual bool claims(std::string_view type) const = 0;

    /// Path stored for `type` / `data`, or empty when this spec is not ours.
    [[nodiscard]] virtual std::string pathOf(std::string_view type, const nlohmann::json& data) const = 0;

    /// Read-only property discovery. Does not run lifecycle callbacks.
    virtual bool loadPreview(std::string path, ScriptInstance& instance) const = 0;
};

YA_ECS_SYSTEMS_API void                setUILogicAuthoring(IUILogicAuthoring* authoring);
YA_ECS_SYSTEMS_API IUILogicAuthoring*  uiLogicAuthoring();

} // namespace ya
