#pragma once

#include "Gameplay/Linkage/LinkageRule.h"

#include <glm/glm.hpp>
#include <string>

namespace ya
{

class LinkageFramework;
struct BillboardComponent;
struct Scene;

/// Render/editor adapter policy for light billboards, injected by the Host
/// (which owns the config source). The rule never reaches Host config.
struct LightBillboardConfig
{
    bool        enabled          = true;
    float       screenSizePixels = 30.0f;
    float       minWorldScale    = 0.4f;
    glm::vec4   tint             = glm::vec4(1.0f);
    std::string texturePath      = "Engine:Content/TestTextures/icons8-light-64.png";
};

struct LightBillboardPolicy
{
    LightBillboardConfig point       = LightBillboardConfig{true, 30.0f, 0.4f, glm::vec4(1.0f, 0.95f, 0.45f, 1.0f), "Engine:Content/TestTextures/icons8-light-64.png"};
    LightBillboardConfig directional = LightBillboardConfig{true, 42.0f, 1.0f, glm::vec4(1.0f, 0.96f, 0.72f, 1.0f), "Engine:Content/TestTextures/icons8-light-64.png"};
};

/**
 * @brief Light <-> billboard linkage rule (render adapter).
 *
 * Watches point/directional light components (+ billboard removal) and keeps
 * a light-managed BillboardComponent in sync, using the framework's deferred
 * scheduler. This is business logic; the linkage framework stays generic.
 */
struct LightBillboardLinkageRule : public ILinkageRule
{
    explicit LightBillboardLinkageRule(LinkageFramework* framework);

    /// Injected by the Host once at startup (config source stays in Host).
    /// Interim: stored on the rule (not the framework); the automation RPC
    /// still reaches the policy through a static accessor.
    void setPolicy(LightBillboardPolicy policy);
    static const LightBillboardPolicy& policy();

    void onSceneInit(Scene* scene) override;
    void onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type) override;

    /// Immediate (non-deferred) linkage, used by the automation RPC.
    static void applyLinkage(Scene* scene, const entt::entity entity);

  private:
    void onLightEvent(entt::registry& reg, entt::entity entity);

    LinkageFramework* _framework = nullptr;
};

} // namespace ya
