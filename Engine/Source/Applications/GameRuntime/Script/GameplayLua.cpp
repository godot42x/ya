#include "GameRuntime/Script/GameplayLua.h"
#include "GameRuntime/Script/GameplayScriptFunctions.h"

#include "LuaWidgetHandle.h"
#include "LuaWidgetScripts.h"

#include "GameRuntime/GUI/GameUI/GameUIHost.h"
#include "GameRuntime/InputRouter.h"
#include "Core/Event.h"
#include "Core/Log.h"

#include "Scene2D/Sprite2DComponent.h"
#include "ECS/Entity.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "Script/Lua/LuaScriptBinding.h"
#include "Script/Lua/LuaScriptingSystem.h"
#include "Scene/Core/Scene.h"
#include "Scene/Core/SceneScriptBindings.h"
#include "Scene3D/Node3D.h"

namespace ya
{
namespace
{

constexpr const char* kSpriteTexture = "Engine:Content/TestTextures/sprite2d_probe_64.png";

/// `texturePath` empty keeps the probe image. A caller-supplied path is stored
/// through `TextureSlot::fromPath` (the same asset-path form as a scene sprite).
Entity* spawnSprite(const GameplayScriptContext& ctx, const std::string& name, const std::string& texturePath)
{
    Scene* scene = ctx.activeScene ? ctx.activeScene() : nullptr;
    if (!scene) {
        YA_CORE_ERROR("world.spawnSprite: no active scene");
        return nullptr;
    }
    Node3D* node = scene->createNode3D(name.empty() ? "Sprite" : name);
    if (!node || !node->getEntity()) {
        return nullptr;
    }
    Sprite2DComponent* sprite = node->getEntity()->addComponent<Sprite2DComponent>();
    sprite->image.fromPath(texturePath.empty() ? kSpriteTexture : texturePath);
    sprite->size = {1.0f, 1.0f};
    return node->getEntity();
}

void destroySpriteEntity(const GameplayScriptContext& ctx, Entity* entity)
{
    Scene* scene = ctx.activeScene ? ctx.activeScene() : nullptr;
    if (!scene || !entity) {
        return;
    }
    if (Node* node = scene->getNodeByEntity(entity)) {
        scene->queueDestroyNode(node);
    }
}

} // namespace

void bindGameplayLua(LuaScriptingSystem& scripting, GameUIHost& ui, GameplayScriptContext ctx)
{
    sol::state& lua = scripting.lua();

    sol::table inputType = lua["Input"];
    // The router is the one injected at bind time. The callback itself is
    // unchanged: (key, pressed, repeat) -> bool, true consumes the event.
    inputType.set_function("setKeyHandler", [router = ctx.inputRouter](sol::object /*self*/, sol::object handler) {
        if (!router) {
            return;
        }
        if (!handler.is<sol::protected_function>()) {
            router->setGameKeyHandler(nullptr);
            return;
        }
        sol::protected_function callback = handler.as<sol::protected_function>();
        if (!callback.valid()) {
            router->setGameKeyHandler(nullptr);
            return;
        }
        router->setGameKeyHandler([callback](const Event& event) {
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

    // Module functions registered through the script export already live here.
    sol::table world = lua["world"].get_or_create<sol::table>();
    // Second argument is optional. Omitted or blank keeps the probe texture so
    // existing one-argument callers (GreedySnake) stay valid.
    world.set_function("spawnSprite", [ctx](const std::string& name, sol::optional<std::string> texturePath) -> sol::optional<LuaScriptObject> {
        const std::string& path = texturePath.value_or("");
        Entity* entity = spawnSprite(ctx, name, path);
        return entity ? sol::optional<LuaScriptObject>(LuaScriptObject{script::entityRef(entity)}) : sol::nullopt;
    });
    world.set_function("destroyEntity", [ctx](LuaScriptObject entity) {
        destroySpriteEntity(ctx, script::entityOf(entity.ref));
    });
    world.set_function("viewAspect", [ctx]() {
        const GameplayPresentedView presented = gameplayPresentedView(ctx);
        return scriptViewAspect(presented.camera, presented.pixelExtent);
    });

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
