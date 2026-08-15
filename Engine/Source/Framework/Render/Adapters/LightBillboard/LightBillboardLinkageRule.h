#pragma once

#include "Core/Api.h"
#include "ECS/Linkage/LinkageRule.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_set>

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
struct YA_RENDER_ECS_ADAPTERS_API LightBillboardLinkageRule : public ILinkageRule
{
    explicit LightBillboardLinkageRule(LinkageFramework* framework);
    ~LightBillboardLinkageRule() override;

    /// Injected by the Host once at startup (config source stays in Host);
    /// stored on this rule instance, never on shared global state.
    void setPolicy(LightBillboardPolicy policy);
    [[nodiscard]] const LightBillboardPolicy& policy() const { return _policy; }

    void onSceneInit(Scene* scene) override;
    void onComponentRemoved(entt::registry& reg, entt::entity entity, ya::type_index_t type) override;
    void onSceneUnload(Scene* scene) override;

    /// Immediate (non-deferred) linkage, used by the automation RPC. The
    /// policy is passed explicitly because the RPC path has no rule instance.
    static void applyLinkage(Scene* scene, const entt::entity entity, const LightBillboardPolicy& policy);

  private:
    void onLightEvent(entt::registry& reg, entt::entity entity);
    void disconnectScene(entt::registry& registry);

    LinkageFramework* _framework = nullptr;
    LightBillboardPolicy _policy;
    /// Registries this rule connected entt signals to; used to disconnect on
    /// scene unload and when the rule is destroyed (framework shutdown).
    std::unordered_set<entt::registry*> _connectedRegistries;
};

} // namespace ya
