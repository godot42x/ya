#include "GameRuntime/Script/GameplayLua.h"

#include "LuaWidgetHandle.h"
#include "LuaWidgetScripts.h"

#include "GameRuntime/App.h"
#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GameRuntime/InputRouter.h"
#include "Core/Event.h"
#include "Core/Log.h"

#include "ECS/Component/2D/Sprite2DComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/LuaScriptingSystem.h"
#include "Scene/Core/Scene.h"
#include "Scene3D/Node3D.h"

namespace ya
{
namespace
{

constexpr const char* kSpriteTexture = "Engine:Content/TestTextures/sprite2d_probe_64.png";

Scene* activeScene()
{
    App* app = App::get();
    return app ? app->getSceneServices().getActiveScene() : nullptr;
}

Entity* spawnSprite(const std::string& name)
{
    Scene* scene = activeScene();
    if (!scene) {
        YA_CORE_ERROR("world.spawnSprite: no active scene");
        return nullptr;
    }
    Node3D* node = scene->createNode3D(name.empty() ? "Sprite" : name);
    if (!node || !node->getEntity()) {
        return nullptr;
    }
    Sprite2DComponent* sprite = node->getEntity()->addComponent<Sprite2DComponent>();
    sprite->image.fromPath(kSpriteTexture);
    sprite->size = {1.0f, 1.0f};
    return node->getEntity();
}

void destroySpriteEntity(Entity* entity)
{
    Scene* scene = activeScene();
    if (!scene || !entity) {
        return;
    }
    if (Node* node = scene->getNodeByEntity(entity)) {
        scene->queueDestroyNode(node);
    }
}

/// Width / height of the presented view. The playfield stays a fixed world
/// rectangle; a script uses this only to fit that rectangle in frame.
float viewAspect()
{
    App* app = App::get();
    if (!app) {
        return 1.0f;
    }
    const Extent2D resolution = app->getRenderServices().getRenderResolution();
    if (resolution.width == 0 || resolution.height == 0) {
        return 1.0f;
    }
    return static_cast<float>(resolution.width) / static_cast<float>(resolution.height);
}

} // namespace

void bindGameplayLua(LuaScriptingSystem& scripting, GameUIHost& ui)
{
    sol::state& lua = scripting.lua();

    lua.new_usertype<glm::vec4>("Vec4",
                                sol::constructors<glm::vec4(), glm::vec4(float), glm::vec4(float, float, float, float)>(),
                                "x",
                                &glm::vec4::x,
                                "y",
                                &glm::vec4::y,
                                "z",
                                &glm::vec4::z,
                                "w",
                                &glm::vec4::w);

    lua.new_usertype<Sprite2DComponent>("Sprite2DComponent",
                                        sol::no_constructor,
                                        "bVisible",
                                        &Sprite2DComponent::bVisible,
                                        "size",
                                        &Sprite2DComponent::size,
                                        "tint",
                                        &Sprite2DComponent::tint);

    sol::table inputType = lua["Input"];
    inputType.set_function("setKeyHandler", [](sol::object /*self*/, sol::object handler) {
        App* app = App::get();
        if (!app) {
            return;
        }
        InputRouter& router = app->getInputRouter();
        if (!handler.is<sol::protected_function>()) {
            router.setGameKeyHandler(nullptr);
            return;
        }
        sol::protected_function callback = handler.as<sol::protected_function>();
        if (!callback.valid()) {
            router.setGameKeyHandler(nullptr);
            return;
        }
        router.setGameKeyHandler([callback](const Event& event) {
            const bool pressed = event.getEventType() == EEvent::KeyPressed;
            EKey::T    key     = EKey::NONE;
            bool       repeat  = false;
            if (pressed) {
                const auto& keyEvent = static_cast<const KeyPressedEvent&>(event);
                key                  = keyEvent.getKeyCode();
                repeat               = keyEvent.isRepeat();
            }
            else {
                key = static_cast<const KeyReleasedEvent&>(event).getKeyCode();
            }
            const sol::protected_function_result result = callback(static_cast<int>(key), pressed, repeat);
            if (!result.valid()) {
                const sol::error error = result;
                YA_CORE_ERROR("Lua key handler: {}", error.what());
                return false;
            }
            const sol::object value = result.get<sol::object>();
            return value.is<bool>() && value.as<bool>();
        });
    });

    sol::table entityType = lua["Entity"];
    entityType.set_function("hasSprite", [](Entity& entity) { return entity.hasComponent<Sprite2DComponent>(); });
    entityType.set_function("getSprite", [](Entity& entity) -> Sprite2DComponent* {
        return entity.hasComponent<Sprite2DComponent>() ? entity.getComponent<Sprite2DComponent>() : nullptr;
    });

    sol::table world = lua.create_named_table("world");
    world.set_function("spawnSprite", [](const std::string& name) -> Entity* { return spawnSprite(name); });
    world.set_function("destroyEntity", [](Entity* entity) { destroySpriteEntity(entity); });
    world.set_function("viewAspect", []() { return viewAspect(); });

    ui.setBehaviorRuntime(std::make_unique<LuaWidgetScripts>(scripting, ui));

    sol::table uiTable = lua.create_named_table("ui");
    // The entry's interface: its root script's `self` when it has one (so
    // gameplay calls methods the UI script defines), else the root handle.
    uiTable.set_function("get", [&ui](const std::string& entryId, sol::this_state state) -> sol::object {
        UIElementRef root = ui.findEntryRoot(entryId);
        if (!root) {
            YA_CORE_WARN("ui.get: no mounted Game UI entry '{}'", entryId);
            return sol::make_object(state, sol::lua_nil);
        }
        sol::object self = LuaWidgetScripts::scriptSelfOf(*root);
        return self.valid() ? self : makeLuaWidgetHandle(state, ui, root);
    });
    uiTable.set_function("setText", [&ui](const std::string& entryId, const std::string& widgetName, const std::string& text) {
        return ui.setMountedText(entryId, widgetName, text);
    });
    uiTable.set_function("setVisible", [&ui](const std::string& entryId, const std::string& widgetName, bool visible) {
        return ui.setMountedVisible(entryId, widgetName, visible);
    });
}

} // namespace ya
