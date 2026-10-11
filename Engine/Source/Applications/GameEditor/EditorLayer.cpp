#include "Core/Input/InputManager.h"
#include "GameEditor/EditorLayerInternal.h"
#include "GameEditor/EditorUIDesignerSession.h"
#include "GUI/Widgets/UIDocument.h"
#include "GUI/Widgets/UIDocumentStore.h"
#include "ECS/Component.h"
#include "ECS/Systems/Components/CameraComponent.h"
#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"
#include "Scene/Core/Scene.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ya
{
EditorLayer::EditorLayer(App* app)
    : _app(app),
      _selection(this),
      _uiDesignerSession(this)
{
    _gizmo.bind(_app, FEditorViewportGizmoSources{
        .getSelectedEntity = [this]() { return getSelectedEntity(); },
        .getSelections = [this]() -> const std::vector<Entity*>& { return getSelections(); },
        .getViewportInteractionScene = [this]() { return getViewportInteractionScene(); },
        .isEditorOrthoXY = [this]() { return isEditorOrthoXY(); },
        .onTransformCommitted = [this]() { markSceneDirty(); },
    });
    _tileBrush.bind(FTileBrushSources{
        .getSelectedEntity = [this]() { return getSelectedEntity(); },
        .getViewportInteractionScene = [this]() { return getViewportInteractionScene(); },
        .isEditorOrthoXY = [this]() { return isEditorOrthoXY(); },
        .onTilesCommitted = [this]() { markSceneDirty(); },
    });
    syncDesignResolutionFromProject();
}

void EditorLayer::syncDesignResolutionFromProject()
{
    glm::uvec2 size{1280, 720};
    if (_app) {
        if (const GameUIHost* ui = _app->getGameUIHost()) {
            const glm::uvec2 ref = ui->referenceResolution();
            if (ref.x > 0 && ref.y > 0) {
                size = ref;
            }
        }
    }
    _uiDesignerSession.setDesignResolution(size);
}

void EditorLayer::bindEditorInput(InputManager& input)
{
    _editorInput = &input;
    _gizmo.setEditorInput(&input);
}

bool EditorLayer::shouldCaptureInput() const
{
    if (bViewportFocused || bViewportHovered) {
        return true;
    }
    return _editorInput && _editorInput->isMouseButtonPressed(EMouse::Right);
}

void EditorLayer::addViewportShown()
{
    ++_shownViewportCount;
}

void EditorLayer::removeViewportShown()
{
    if (_shownViewportCount > 0) {
        --_shownViewportCount;
    }
    if (_shownViewportCount == 0) {
        // The last viewport widget left the tree, so nothing can hover or focus
        // it. Keeping the last frame's answer would leave the editor camera
        // eating input -- WASD and the camera controller -- meant for whichever
        // tab replaced it.
        bViewportHovered = false;
        bViewportFocused = false;
    }
}

void EditorLayer::onAttach()
{
    YA_PROFILE_FUNCTION();
    YA_CORE_INFO("EditorLayer::onAttach");

    if (!_app)
        return;

    syncEditorSettingsFromConfig();

    // Initialize editor panels
    if (auto scene = getEditableScene())
    {
        _selection.setContext(scene);
        notifyHierarchyChanged();
    }

    if (!hasProjectLoaded()) {
        refreshProjectBrowser();
    }
}

void EditorLayer::onDetach()
{
    YA_CORE_INFO("EditorLayer::onDetach");
    _uiDesignerSession.abandonDocument();
    // Unsubscribe from scene manager events
    if (_app) {
        if (auto* sceneManager = _app->getSceneServices().getSceneManager()) {
            sceneManager->onSceneActivated.removeAll(this);
        }
    }
}

void EditorLayer::onUpdate(float dt)
{
    YA_PROFILE_FUNCTION();
    _lastDeltaTime = dt;
}

Entity* EditorLayer::getCameraPreviewEntity() const
{
    Entity* selected = getSelectedEntity();
    if (!selected || !selected->isValid() || !selected->hasComponent<CameraComponent>() ||
        !selected->hasComponent<TransformComponent>()) {
        return nullptr;
    }
    return selected;
}

void EditorLayer::setEditableScene(Scene* scene)
{
    _editableScene = scene;
    _selection.setContext(getSceneHierarchyContext());
}

void EditorLayer::bindDocumentServices(EditorDocumentRegistry* documents, UIDocumentStore* uiDocuments)
{
    _documents       = documents;
    _uiDocumentStore = uiDocuments;
}

void EditorLayer::setCurrentScenePath(std::string scenePath)
{
    if (_currentScenePath == scenePath) {
        return;
    }
    _currentScenePath = std::move(scenePath);
    onScenePathChanged.broadcast();
}

void EditorLayer::setEditorOrthoXY(bool enabled)
{
    if (_bEditorOrthoXY == enabled) {
        return;
    }
    _bEditorOrthoXY = enabled;
    if (!enabled) {
        return;
    }
    FreeCamera& camera   = getCamera();
    glm::vec3   position = camera.getPosition();
    if (std::abs(position.z) < 1.0f) {
        position.z = 20.0f;
    }
    camera.setPositionAndRotation(position, {0.0f, 0.0f, 0.0f});
}

namespace
{

std::string joinVfsPath(std::string directory, const std::string& file)
{
    if (directory.empty()) {
        directory = "Content:UI";
    }
    if (directory.back() != '/') {
        directory.push_back('/');
    }
    return directory + file;
}

bool assetNameOk(const std::string& name)
{
    return !name.empty() && name.find_first_of("/\\:") == std::string::npos;
}

std::string suggestGameUIName(UIDocumentStore* store, Scene* scene, const std::string& directory)
{
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    for (int index = 1; index < 1000; ++index) {
        const std::string name = index == 1 ? std::string("Panel") : std::format("Panel{}", index);
        const std::string path = joinVfsPath(directory, name + ".yaui.json");
        if (store && store->find(path)) {
            continue;
        }
        if (vfs && vfs->isFileExists(path)) {
            continue;
        }
        bool mounted = false;
        if (scene) {
            for (const SceneWidgetEntry& entry : scene->getWidgetEntries()) {
                if (entry.entryId == name || entry.documentPath == path) {
                    mounted = true;
                    break;
                }
            }
        }
        if (!mounted) {
            return name;
        }
    }
    return "Panel";
}

int32_t nextGameUIZOrder(const Scene& scene)
{
    bool    any = false;
    int32_t max = 0;
    for (const SceneWidgetEntry& entry : scene.getWidgetEntries()) {
        if (!any || entry.zOrder > max) {
            max = entry.zOrder;
        }
        any = true;
    }
    return any ? max + 10 : 10;
}

} // namespace

const char* EditorLayer::uiScriptTemplate()
{
    return R"(local S = {}

function S:onInit()
    local button = self:find("Button")
    if button then
        button.onClicked:add(self, function() end)
    end
end

function S:onUpdate(dt)
end

function S:onCancel()
    return false
end

return S
)";
}

void EditorLayer::requestName(FEditorNamePrompt request)
{
    if (!_namePromptHandler) {
        YA_CORE_WARN("EditorLayer::requestName: no name prompt for '{}'", request.title);
        return;
    }
    _namePromptHandler(std::move(request));
}

void EditorLayer::createAndMountGameUI()
{
    const std::string directory = "Content:UI";
    requestName({
        .title   = "New Game UI",
        .label   = "Name",
        .initial = suggestGameUIName(uiDocumentStore(), getEditableScene(), directory),
        .onConfirm = [this, directory](std::string name) {
            (void)createAndMountGameUIAt(directory, std::move(name));
        },
    });
}

bool EditorLayer::createAndMountGameUIAt(std::string vfsDirectory, std::string name)
{
    Scene*           scene = getEditableScene();
    UIDocumentStore* store = uiDocumentStore();
    if (!scene || !store || !assetNameOk(name)) {
        YA_CORE_WARN("createAndMountGameUIAt: need a scene, a document store, and a name");
        return false;
    }
    const std::string path = joinVfsPath(std::move(vfsDirectory), name + ".yaui.json");
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if ((vfs && vfs->isFileExists(path)) || store->find(path)) {
        YA_CORE_WARN("createAndMountGameUIAt: '{}' already exists", path);
        return false;
    }
    for (const SceneWidgetEntry& entry : scene->getWidgetEntries()) {
        if (entry.entryId == name || entry.documentPath == path) {
            YA_CORE_WARN("createAndMountGameUIAt: '{}' is already mounted", name);
            return false;
        }
    }

    auto document    = std::make_shared<UIDocument>();
    document->typeId = "engine.panel";
    document->fields = nlohmann::json{
        {"__base__", {{"UIElement", {{"_name", name}}}}},
    };
    store->put(path, document);
    (void)store->save(path);

    SceneWidgetEntry entry;
    entry.entryId      = name;
    entry.documentPath = path;
    entry.zOrder       = nextGameUIZOrder(*scene);
    entry.autoMount    = true;
    scene->addWidgetEntry(std::move(entry));
    markSceneDirty();
    notifyHierarchyChanged();
    setSelectedWidgetEntryId(name);
    (void)openDocumentEditor(EEditorDocumentKind::UI, path);
    return true;
}

bool EditorLayer::createUiScript(std::string vfsDirectory, std::string name)
{
    if (!assetNameOk(name)) {
        YA_CORE_WARN("createUiScript: empty or illegal name");
        return false;
    }
    if (!name.ends_with(".lua")) {
        name += ".lua";
    }
    const std::string path = joinVfsPath(std::move(vfsDirectory), name);
    VirtualFileSystem* vfs = VirtualFileSystem::get();
    if (!vfs) {
        YA_CORE_ERROR("createUiScript: no virtual file system for '{}'", path);
        return false;
    }
    if (vfs->isFileExists(path)) {
        YA_CORE_WARN("createUiScript: '{}' already exists", path);
        return false;
    }
    vfs->saveToFile(path, uiScriptTemplate());
    return vfs->isFileExists(path);
}

void EditorLayer::setSelectedWidgetEntryModal(bool value)
{
    SceneWidgetEntry* entry = getSelectedWidgetEntry();
    if (!entry || entry->bModal == value) {
        return;
    }
    entry->bModal = value;
    markSceneDirty();
}

void EditorLayer::setSelectedWidgetEntryPausesGame(bool value)
{
    SceneWidgetEntry* entry = getSelectedWidgetEntry();
    if (!entry || entry->bPausesGame == value) {
        return;
    }
    entry->bPausesGame = value;
    markSceneDirty();
}

void EditorLayer::setSelectedWidgetEntryClock(ESceneWidgetClock clock)
{
    SceneWidgetEntry* entry = getSelectedWidgetEntry();
    if (!entry || entry->clock == clock) {
        return;
    }
    entry->clock = clock;
    markSceneDirty();
}

void EditorLayer::setSelectedWidgetEntryZOrder(int32_t zOrder)
{
    SceneWidgetEntry* entry = getSelectedWidgetEntry();
    if (!entry || entry->zOrder == zOrder) {
        return;
    }
    entry->zOrder = zOrder;
    markSceneDirty();
    notifyHierarchyChanged();
}

void EditorLayer::mountOpenGameUI()
{
    const std::string path = _uiDesignerSession.getDocumentPath();
    Scene*            scene = getEditableScene();
    if (path.empty() || !scene) {
        return;
    }
    for (const SceneWidgetEntry& existing : scene->getWidgetEntries()) {
        if (existing.documentPath == path) {
            setSelectedWidgetEntryId(existing.entryId);
            return;
        }
    }

    std::string entryId = std::filesystem::path(path).stem().stem().string();
    if (entryId.empty()) {
        entryId = "GameUI";
    }
    const std::string baseId = entryId;
    for (int suffix = 2; suffix < 1000; ++suffix) {
        bool taken = false;
        for (const SceneWidgetEntry& existing : scene->getWidgetEntries()) {
            if (existing.entryId == entryId) {
                taken = true;
                break;
            }
        }
        if (!taken) {
            break;
        }
        entryId = std::format("{}{}", baseId, suffix);
    }

    SceneWidgetEntry entry;
    entry.entryId      = entryId;
    entry.documentPath = path;
    entry.zOrder       = static_cast<int32_t>(scene->getWidgetEntries().size());
    entry.autoMount    = true;
    scene->addWidgetEntry(std::move(entry));
    markSceneDirty();
    notifyHierarchyChanged();
    setSelectedWidgetEntryId(entryId);
}

bool EditorLayer::canMountOpenGameUI() const
{
    const std::string& path  = _uiDesignerSession.getDocumentPath();
    Scene*             scene = getEditableScene();
    if (path.empty() || !scene) {
        return false;
    }
    for (const SceneWidgetEntry& entry : scene->getWidgetEntries()) {
        if (entry.documentPath == path) {
            return false;
        }
    }
    return true;
}

void EditorLayer::openGameUIEntry(const std::string& entryId)
{
    Scene* scene = getEditableScene();
    if (!scene || entryId.empty()) {
        return;
    }
    for (const SceneWidgetEntry& entry : scene->getWidgetEntries()) {
        if (entry.entryId == entryId) {
            (void)openDocumentEditor(EEditorDocumentKind::UI, entry.documentPath);
            return;
        }
    }
}

void EditorLayer::unmountGameUIEntry(const std::string& entryId)
{
    Scene* scene = getEditableScene();
    if (!scene || !scene->removeWidgetEntry(entryId)) {
        return;
    }
    if (_selectedWidgetEntryId == entryId) {
        setSelectedWidgetEntryId({});
    }
    markSceneDirty();
    notifyHierarchyChanged();
}


void EditorLayer::notifyViewportWidgetRect(const Rect2D& rect, const Rect2D& previewPanelRect)
{
    _viewportBounds[0] = rect.pos;
    _viewportBounds[1] = rect.pos + rect.extent;
    viewportRect       = rect;
    _viewportPreviewPanelRect = previewPanelRect;
    _viewportMouseRect = rect;
    _viewportMouseCenter = {
        rect.pos.x + rect.extent.x * 0.5f,
        rect.pos.y + rect.extent.y * 0.5f,
    };

    // Panel geometry only. The editor declares its authoring View with this
    // rect, so a change here is a declaration fact, not something to push into
    // host state and read back. The default only follows a rect that can hold a
    // whole pixel: the tab reports zero-sized geometry while it is collapsed or
    // not yet laid out, and that must not become the editor's view size.
    if (describesPixels(rect)) {
        _viewportSize = rect.extent;
    }
}

void EditorLayer::setViewportHoverFocus(bool hovered, bool focused)
{
    bViewportHovered = hovered;
    bViewportFocused = focused;
}

void EditorLayer::runAfterUnsavedResolved(std::function<void()> proceed)
{
    if (!proceed) {
        return;
    }
    if (!_bSceneDirty) {
        proceed();
        return;
    }
    if (_unsavedGuard) {
        _unsavedGuard(std::move(proceed));
        return;
    }
    proceed();
}

void EditorLayer::cmdNewScene()
{
    runAfterUnsavedResolved([this]() {
        auto* app = App::get();
        if (!app) {
            return;
        }
        app->getTaskManager().registerTickTask([this]() {
            auto* taskApp = App::get();
            if (!taskApp) {
                return;
            }

            auto* sceneManager = taskApp->getSceneServices().getSceneManager();
            if (sceneManager && sceneManager->hasScene()) {
                if (auto* render = taskApp->getRenderServices().getRender()) {
                    render->waitIdle();
                }
            }
            auto scene = makeShared<Scene>();
            if (sceneManager) {
                sceneManager->unloadScene();
                sceneManager->activateScene(scene);
            }
            setCurrentScenePath({});
            clearSceneDirty();
        });
    });
}

void EditorLayer::cmdLoadScene(std::string scenePath)
{
    if (scenePath.empty()) {
        return;
    }
    runAfterUnsavedResolved([this, scenePath = std::move(scenePath)]() {
        if (!_app || scenePath.empty()) {
            return;
        }
        _app->getTaskManager().registerTickTask([this, scenePath]() {
            if (!_app) {
                return;
            }
            if (_app->getSceneServices().loadScene(scenePath)) {
                setCurrentScenePath(scenePath);
                clearSceneDirty();
            }
        });
    });
}

void EditorLayer::cmdSaveScene()
{
    if (!_currentScenePath.empty()) {
        if (_app && _app->getSceneServices().getSceneManager()) {
            if (auto* scene = getEditableScene()) {
                (void)scene;
                if (_app->getSceneServices().saveScene(_currentScenePath)) {
                    YA_CORE_INFO("Scene saved to: {}", _currentScenePath);
                    clearSceneDirty();
                }
            }
        }
        return;
    }
    cmdSaveSceneAs();
}

void EditorLayer::cmdSaveSceneAs()
{
    if (_saveSceneAsHandler) {
        _saveSceneAsHandler();
        return;
    }

    std::string defaultName = "NewScene";
    if (_app && _app->getSceneServices().getSceneManager()) {
        if (auto* scene = getEditableScene(); scene && !scene->getName().empty()) {
            defaultName = scene->getName();
        }
    }

    _filePicker.openSceneSavePicker(
        defaultName,
        [this](const std::string& selectedDir, const std::string& sceneName) {
            _currentScenePath = selectedDir + "/" + sceneName + ".scene.json";
            if (_app && _app->getSceneServices().getSceneManager()) {
                if (auto* scene = getEditableScene()) {
                    scene->setName(sceneName);
                    if (_app->getSceneServices().saveScene(_currentScenePath)) {
                        YA_CORE_INFO("Scene saved to: {}", _currentScenePath);
                        clearSceneDirty();
                    }
                }
            }
        });
}

void EditorLayer::cmdOpenScene()
{
    runAfterUnsavedResolved([this]() {
        auto onPicked = [this](std::string path) {
            if (path.empty()) {
                return;
            }
            if (!_app) {
                return;
            }
            _app->getTaskManager().registerTickTask([this, path = std::move(path)]() {
                if (!_app) {
                    return;
                }
                if (_app->getSceneServices().loadScene(path)) {
                    setCurrentScenePath(path);
                    clearSceneDirty();
                }
            });
        };
        if (_filePickerHandler) {
            _filePickerHandler(makeSceneOpenPickerRequest(_currentScenePath, std::move(onPicked)));
            return;
        }
        _filePicker.open("Open Scene", _currentScenePath, {".scene.json"}, std::move(onPicked));
    });
}

void EditorLayer::cmdRequestQuit()
{
    runAfterUnsavedResolved([]() {
        if (auto* app = App::get()) {
            app->requestQuit();
        }
    });
}

} // namespace ya
