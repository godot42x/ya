#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "Core/Log.h"

#include "GameRuntime/GUI/GameUI/DefaultGameUIController.h"

#include "Resource/AssetManager.h"
#include "RHI/Core/Texture.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/GuiTextureCatalog.h"
#include "GUI/Widgets/UIDocumentStore.h"

#include "Scene/Core/Scene.h"

#include <algorithm>
#include <format>

namespace ya
{

namespace
{

struct AssetGuiTextureSource final : IGuiTextureSource
{
    [[nodiscard]] FGuiTextureLookup lookup(const std::string& path) override
    {
        AssetManager* assets = AssetManager::get();
        if (!assets || path.empty()) {
            return {};
        }
        if (auto texture = assets->getTextureByPath(path)) {
            return {std::move(texture), EGuiTextureState::Ready};
        }
        if (assets->isTextureLoadFailed(path)) {
            return {nullptr, EGuiTextureState::Failed};
        }
        return {nullptr, EGuiTextureState::Pending};
    }

    void requestLoad(const std::string& path, FGuiTextureReady ready) override
    {
        AssetManager* assets = AssetManager::get();
        if (!assets || path.empty()) {
            if (ready) {
                ready(path, {nullptr, EGuiTextureState::Failed});
            }
            return;
        }
        const std::string normalized = AssetManager::normalizeAssetPath(path);
        // Async only: snapshot/paint must not create GPU resources (Core Rule 6).
        // Catalog kick-once plus AssetManager pending-dedup keep one load per path.
        assets->loadTexture(AssetManager::TextureLoadRequest{
            .filepath = normalized,
            .name     = {},
            .onReady  = [ready, path](const std::shared_ptr<Texture>& texture) {
                if (!ready) {
                    return;
                }
                if (texture) {
                    ready(path, {texture, EGuiTextureState::Ready});
                    return;
                }
                ready(path, {nullptr, EGuiTextureState::Failed});
            },
        });
    }

    [[nodiscard]] uint64_t epoch() const override
    {
        AssetManager* assets = AssetManager::get();
        return assets ? assets->getResourceVersionEpoch() : 0;
    }

    /// AssetManager::loadTexture onReady always hops to the game/UI thread.
    [[nodiscard]] EGuiTextureCompletionThread completionThread() const override
    {
        return EGuiTextureCompletionThread::UIOwner;
    }
};

} // namespace

IGuiTextureSource& gameUITextureSource()
{
    static AssetGuiTextureSource source;
    return source;
}

GameUIHost::GameUIHost() : _controller(std::make_unique<DefaultGameUIController>())
{
    _tree.setTextureSource(&gameUITextureSource());
}

GameUIHost::~GameUIHost() = default;

void GameUIHost::setPresentation(const Rect2D& viewportPx, const glm::vec2& framebufferScale)
{
    _viewportPx         = viewportPx;
    _framebufferScale   = framebufferScale;
    const float width   = std::max(viewportPx.extent.x, 1.0f) / std::max(framebufferScale.x, 0.01f);
    const float height  = std::max(viewportPx.extent.y, 1.0f) / std::max(framebufferScale.y, 0.01f);
    _tree.setLogicalExtent(Extent2D::fromVec2({width, height}));
}

void GameUIHost::setController(std::unique_ptr<IGameUIController> controller)
{
    if (!controller) {
        YA_CORE_WARN("GameUIHost::setController: null controller ignored");
        return;
    }
    if (_mountedScene) {
        // Handover: the old controller unmounts what it created, then the new
        // controller mounts the currently presented scene.
        _controller->onSceneDeactivated(*_mountedScene, *this);
    }
    _controller = std::move(controller);
    if (_mountedScene) {
        _controller->onSceneActivated(*_mountedScene, *this);
    }
}

void GameUIHost::onSceneActivated(Scene& scene)
{
    if (_mountedScene == &scene) {
        return;
    }
    if (_mountedScene) {
        onSceneDeactivated(*_mountedScene);
    }
    _mountedScene = &scene;
    _controller->onSceneActivated(scene, *this);
}

void GameUIHost::onSceneDeactivated(Scene& scene)
{
    if (_mountedScene != &scene) {
        // Not the presented world (or already unmounted): still let the
        // controller clean up anything it created for this scene.
        _controller->onSceneDeactivated(scene, *this);
        return;
    }
    _controller->onSceneDeactivated(scene, *this);
    _mountedScene = nullptr;
}

void GameUIHost::reloadMountedSceneUI()
{
    if (!_mountedScene) {
        return;
    }
    Scene* scene = _mountedScene;
    _controller->onSceneDeactivated(*scene, *this);
    _mountedScene = nullptr;
    _controller->onSceneActivated(*scene, *this);
    _mountedScene = scene;
}

WidgetAttachment GameUIHost::addToWorld(Scene& world, const UIElementRef& widget)
{
    if (_mountedScene != &world) {
        YA_CORE_ERROR("GameUIHost::addToWorld: world '{}' is not the presented scene; "
                      "refusing to mount to another tree",
                      world.getName());
        return {};
    }
    return _controller->addToWorld(world, widget, *this);
}

WidgetAttachment GameUIHost::addToWorld(Scene& world,
                                        const UIElementRef& widget,
                                        const FCanvasSlotArgs& args)
{
    if (_mountedScene != &world) {
        YA_CORE_ERROR("GameUIHost::addToWorld: world '{}' is not the presented scene; "
                      "refusing to mount to another tree",
                      world.getName());
        return {};
    }
    return _controller->addToWorld(world, widget, args, *this);
}

EWidgetRouteResult GameUIHost::dispatchEvent(const Event& event, const glm::vec2& windowPoint)
{
    const glm::vec2 max = _viewportPx.pos + _viewportPx.extent;
    const bool bInViewport =
        windowPoint.x >= _viewportPx.pos.x && windowPoint.x <= max.x &&
        windowPoint.y >= _viewportPx.pos.y && windowPoint.y <= max.y;
    if (!bInViewport) {
        return EWidgetRouteResult::NotHandled;
    }
    const glm::vec2 logicalPoint =
        (windowPoint - _viewportPx.pos) / glm::max(_framebufferScale, glm::vec2(0.01f));
    WidgetEventContext ctx;
    ctx.logicalPoint = logicalPoint;
    return _tree.dispatchEvent(event, ctx);
}

UIFrameSnapshot GameUIHost::buildSnapshot()
{
    UIFrameBuildContext ctx{
        .uiScale         = _framebufferScale,
        .offset          = _viewportPx.pos,
        .textureResolver = &resolveGameUITexture,
    };
    return _tree.buildSnapshot(ctx);
}

std::shared_ptr<Texture> resolveGameUITexture(const std::string& assetPath)
{
    return gameUITextureSource().lookup(assetPath).texture;
}

std::vector<WidgetAttachment> mountSceneAutoMountEntries(Scene&                                       scene,
                                                         WidgetTree&                                  tree,
                                                         UIDocumentStore*                             documents,
                                                         const std::function<void(std::string_view)>& onError)
{
    const auto report = [&onError](const std::string& message) {
        if (onError) {
            onError(message);
        }
        else {
            YA_CORE_ERROR("{}", message);
        }
    };

    std::vector<WidgetAttachment> attachments;
    for (const auto& entry : scene.getWidgetEntries()) {
        if (!entry.autoMount) {
            continue;
        }
        if (!documents) {
            report(std::format("SceneWidgetEntry '{}' cannot mount: no Game UI document store",
                               entry.entryId));
            continue;
        }
        std::shared_ptr<UIDocument> document = documents->resolve(entry.documentPath);
        if (!document) {
            report(std::format("SceneWidgetEntry '{}' cannot mount document '{}'",
                               entry.entryId, entry.documentPath));
            continue;
        }

        UIElementRef widget = document->instantiate();
        if (!widget) {
            report(std::format("SceneWidgetEntry '{}' failed to instantiate", entry.entryId));
            continue;
        }
        widget->_zOrder = entry.zOrder;
        entry.overrides.applyTo(*widget);

        WidgetAttachment attachment = tree.attachToLayer(WidgetTree::ELayer::Content, widget, entry.rootSlot);
        if (attachment.valid()) {
            attachments.push_back(std::move(attachment));
        }
    }
    return attachments;
}

} // namespace ya
