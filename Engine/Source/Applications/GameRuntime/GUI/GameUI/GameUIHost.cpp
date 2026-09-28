#include "GameRuntime/GUI/GameUI/GameUIHost.h"

#include "Core/Log.h"

#include "GameRuntime/GUI/GameUI/DefaultGameUIController.h"

#include "Resource/AssetManager.h"
#include "RHI/Core/Texture.h"

#include "GUI/Layout/UILayout.h"
#include "GUI/Widgets/Controls/Text.h"
#include "GUI/Widgets/GuiTextureCatalog.h"
#include "GUI/Widgets/UIDocumentStore.h"

#include "Scene/Core/Scene.h"

#include <algorithm>
#include <format>
#include <utility>

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
    _tree.setActionSink([this](UIElement& source, std::string_view action) {
        return _worldActionHandler && _worldActionHandler(source, action);
    });
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

void GameUIHost::setBehaviorRuntime(std::unique_ptr<IGameUIBehaviorRuntime> runtime)
{
    // The old runtime outlives the remount, which drops the behaviours it made.
    std::unique_ptr<IGameUIBehaviorRuntime> previous = std::exchange(_behaviorRuntime, std::move(runtime));
    reloadMountedSceneUI();
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
    WidgetAttachment attachment = _controller->addToWorld(world, widget, *this);
    if (attachment.valid()) {
        mountWorldWidget(widget);
    }
    return attachment;
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
    WidgetAttachment attachment = _controller->addToWorld(world, widget, args, *this);
    if (attachment.valid()) {
        mountWorldWidget(widget);
    }
    return attachment;
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

void GameUIHost::update(const FUIFrameClock& clock)
{
    const float deltaSeconds = clock.forClock(_updateClock);
    if (_behaviorRuntime) {
        _behaviorRuntime->update();
    }
    advanceTimers(deltaSeconds);
    _tree.tick(deltaSeconds);
}

void GameUIHost::layoutNow()
{
    if (!_tree.isLayoutValid()) {
        _tree.layout();
    }
}

uint64_t GameUIHost::addTimer(const void* owner, float delaySeconds, float intervalSeconds, std::function<bool()> fire)
{
    if (!fire) {
        return 0;
    }
    const uint64_t id = _nextTimerId++;
    _timers.emplace(id, FTimer{
                            .owner    = owner,
                            .due      = _clockSeconds + std::max(delaySeconds, 0.0f),
                            .interval = std::max(intervalSeconds, 0.0f),
                            .fire     = std::move(fire),
                        });
    return id;
}

void GameUIHost::cancelTimer(uint64_t timerId)
{
    _timers.erase(timerId);
}

void GameUIHost::cancelTimersOf(const void* owner)
{
    std::erase_if(_timers, [owner](const auto& entry) { return entry.second.owner == owner; });
}

void GameUIHost::advanceTimers(float deltaSeconds)
{
    _clockSeconds += deltaSeconds;
    // Snapshot the due ids: a callback may add or cancel timers.
    std::vector<uint64_t> due;
    for (const auto& [id, timer] : _timers) {
        if (timer.due <= _clockSeconds) {
            due.push_back(id);
        }
    }
    for (const uint64_t id : due) {
        auto it = _timers.find(id);
        if (it == _timers.end()) {
            continue;
        }
        const std::function<bool()> fire = it->second.fire;
        const bool bKeep = fire();
        it = _timers.find(id);
        if (it == _timers.end()) {
            continue;
        }
        if (!bKeep || it->second.interval <= 0.0f) {
            _timers.erase(it);
            continue;
        }
        it->second.due = std::max(it->second.due + it->second.interval, _clockSeconds + 1e-6);
    }
}

std::shared_ptr<Texture> resolveGameUITexture(const std::string& assetPath)
{
    return gameUITextureSource().lookup(assetPath).texture;
}

namespace
{

UIElement* findNamedWidget(UIElement* node, std::string_view name)
{
    if (!node) {
        return nullptr;
    }
    if (node->_name == name) {
        return node;
    }
    for (const UIElementRef& child : node->getChildren()) {
        if (UIElement* found = findNamedWidget(child.get(), name)) {
            return found;
        }
    }
    return nullptr;
}

/// Name -> first widget with it, in tree pre-order. Names seen again are
/// recorded as ambiguous (not yet warned) instead of replacing the first.
void indexNamedWidgets(UIElement&                                                  root,
                       std::unordered_map<std::string, std::weak_ptr<UIElement>>& names,
                       std::unordered_map<std::string, bool>&                     ambiguous)
{
    std::vector<UIElement*> pending{&root};
    while (!pending.empty()) {
        UIElement* node = pending.back();
        pending.pop_back();
        if (!node->_name.empty()) {
            auto [it, bInserted] = names.try_emplace(node->_name);
            if (bInserted) {
                it->second = node->weak_from_this();
            }
            else {
                ambiguous.try_emplace(node->_name, false);
            }
        }
        const auto& children = node->getChildren();
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            if (*child) {
                pending.push_back(child->get());
            }
        }
    }
}

} // namespace

std::vector<FSceneUIMount> mountSceneAutoMountEntries(Scene&                                       scene,
                                                      WidgetTree&                                  tree,
                                                      UIDocumentStore*                             documents,
                                                      IUIBehaviorActivator*                        activator,
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

    std::vector<FSceneUIMount> mounts;
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
            if (activator) {
                activateBehaviorSpecs(*widget, *activator, FUIBehaviorActivation{.entryId = entry.entryId, .entryRoot = *widget});
            }
            mounts.push_back(FSceneUIMount{
                .entryId     = entry.entryId,
                .attachment = std::move(attachment),
            });
        }
    }
    return mounts;
}

void GameUIHost::setWorldActionHandler(std::function<bool(UIElement& source, std::string_view action)> handler)
{
    _worldActionHandler = std::move(handler);
}

void GameUIHost::setMountedRoots(std::vector<std::pair<std::string, std::weak_ptr<UIElement>>> roots)
{
    clearMountedRoots();
    for (auto& [entryId, weakRoot] : roots) {
        addEntry(std::move(entryId), weakRoot.lock());
    }
}

void GameUIHost::clearMountedRoots()
{
    // Queued changes target what is being unmounted.
    _entries.clear();
    _pendingSpawns.clear();
    _pendingDestroys.clear();
}

void GameUIHost::addEntry(std::string entryId, const UIElementRef& root)
{
    FMountedEntry& entry = _entries.emplace_back(FMountedEntry{.entryId = std::move(entryId), .root = root});
    if (root) {
        indexNamedWidgets(*root, entry.names, entry.ambiguous);
    }
}

void GameUIHost::mountWorldWidget(const UIElementRef& widget)
{
    // A widget joined by code is its own entry, named after itself.
    addEntry(widget->_name, widget);
    if (_behaviorRuntime) {
        activateBehaviorSpecs(*widget, *_behaviorRuntime, FUIBehaviorActivation{.entryId = widget->_name, .entryRoot = *widget});
    }
}

UIElementRef GameUIHost::queueSpawn(std::string_view documentPath, UIElement& parent)
{
    std::shared_ptr<UIDocument> document = _documents ? _documents->resolve(documentPath) : nullptr;
    UIElementRef                widget   = document ? document->instantiate() : nullptr;
    if (!widget) {
        YA_CORE_WARN("Game UI spawn: document '{}' does not resolve to a widget", documentPath);
        return nullptr;
    }
    _pendingSpawns.push_back(FPendingSpawn{.widget = widget, .parent = parent.weak_from_this()});
    return widget;
}

void GameUIHost::queueDestroy(UIElement& widget)
{
    const auto pending = std::find_if(_pendingSpawns.begin(), _pendingSpawns.end(),
                                      [&widget](const FPendingSpawn& spawn) { return spawn.widget.get() == &widget; });
    if (pending != _pendingSpawns.end()) {
        _pendingSpawns.erase(pending);
        return;
    }
    if (isPendingSpawn(widget)) {
        // Inside a subtree no tree has seen yet: unlinking it now is safe.
        _tree.detach(widget);
        return;
    }
    _pendingDestroys.push_back(widget.weak_from_this());
}

bool GameUIHost::isPendingSpawn(const UIElement& widget) const
{
    const UIElement* top = &widget;
    while (top->getParent()) {
        top = top->getParent();
    }
    return std::any_of(_pendingSpawns.begin(), _pendingSpawns.end(),
                       [top](const FPendingSpawn& spawn) { return spawn.widget.get() == top; });
}

void GameUIHost::flushStructuralChanges()
{
    // A destroy may run onDestroy, which may queue more: those wait for the next flush.
    for (const std::weak_ptr<UIElement>& weak : std::exchange(_pendingDestroys, {})) {
        UIElementRef widget = weak.lock();
        if (!widget || widget->getTree() != &_tree) {
            continue;
        }
        if (UIElement* root = entryRootOf(*widget)) {
            if (FMountedEntry* entry = entryFor(*root)) {
                entry->bIndexDirty = true;
            }
        }
        _tree.detach(*widget);
    }
    for (const FPendingSpawn& spawn : std::exchange(_pendingSpawns, {})) {
        UIElementRef parent = spawn.parent.lock();
        if (!parent || parent->getTree() != &_tree || !_tree.attach(*parent, spawn.widget).valid()) {
            continue;
        }
        UIElement*     root  = entryRootOf(*spawn.widget);
        FMountedEntry* entry = root ? entryFor(*root) : nullptr;
        if (entry) {
            entry->bIndexDirty = true;
        }
        if (_behaviorRuntime) {
            activateBehaviorSpecs(*spawn.widget, *_behaviorRuntime,
                                  FUIBehaviorActivation{
                                      .entryId   = entry ? std::string_view(entry->entryId) : std::string_view{},
                                      .entryRoot = root ? *root : *spawn.widget,
                                  });
        }
    }
}

UIElement* GameUIHost::findMountedWidget(std::string_view entryId, std::string_view widgetName) const
{
    UIElementRef root = findEntryRoot(entryId);
    return findNamedWidget(root.get(), widgetName);
}

UIElementRef GameUIHost::findEntryRoot(std::string_view entryId) const
{
    for (const FMountedEntry& entry : _entries) {
        if (entry.entryId == entryId) {
            UIElementRef root = entry.root.lock();
            return root && root->getTree() == &_tree ? root : nullptr;
        }
    }
    return nullptr;
}

UIElement* GameUIHost::entryRootOf(const UIElement& widget) const
{
    for (const UIElement* node = &widget; node; node = node->getParent()) {
        for (const FMountedEntry& entry : _entries) {
            if (entry.root.lock().get() == node) {
                return const_cast<UIElement*>(node);
            }
        }
    }
    return nullptr;
}

GameUIHost::FMountedEntry* GameUIHost::entryFor(const UIElement& entryRoot)
{
    for (FMountedEntry& entry : _entries) {
        if (entry.root.lock().get() == &entryRoot) {
            return &entry;
        }
    }
    return nullptr;
}

UIElementRef GameUIHost::findInEntry(const UIElement& entryRoot, std::string_view name)
{
    FMountedEntry* entry = entryFor(entryRoot);
    if (!entry) {
        return nullptr;
    }
    if (entry->bIndexDirty) {
        entry->bIndexDirty = false;
        std::unordered_map<std::string, bool> warned = std::exchange(entry->ambiguous, {});
        entry->names.clear();
        if (UIElementRef root = entry->root.lock()) {
            indexNamedWidgets(*root, entry->names, entry->ambiguous);
        }
        for (auto& [ambiguousName, bWarned] : entry->ambiguous) {
            const auto previous = warned.find(ambiguousName);
            bWarned = previous != warned.end() && previous->second;
        }
    }
    const std::string key(name);
    auto it = entry->names.find(key);
    if (it == entry->names.end()) {
        return nullptr;
    }
    if (auto ambiguous = entry->ambiguous.find(key); ambiguous != entry->ambiguous.end() && !ambiguous->second) {
        ambiguous->second = true;
        YA_CORE_WARN("Game UI entry '{}' has more than one widget named '{}'; find returns the first",
                     entry->entryId, key);
    }
    return it->second.lock();
}

bool GameUIHost::setMountedText(std::string_view entryId, std::string_view widgetName, const std::string& text)
{
    auto* textWidget = dynamic_cast<UIText*>(findMountedWidget(entryId, widgetName));
    if (!textWidget) {
        return false;
    }
    textWidget->setText(text);
    return true;
}

bool GameUIHost::setMountedVisible(std::string_view entryId, std::string_view widgetName, bool visible)
{
    UIElement* widget = findMountedWidget(entryId, widgetName);
    if (!widget) {
        return false;
    }
    widget->setVisibility(visible ? EWidgetVisibility::Visible : EWidgetVisibility::Hidden);
    return true;
}

} // namespace ya
