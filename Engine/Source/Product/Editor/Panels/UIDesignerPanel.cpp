#include "Editor/Panels/UIDesignerPanel.h"

#include "Core/Log.h"
#include "Core/System/VirtualFileSystem.h"

#include "Editor/EditorLayer.h"
#include "Editor/Inspector/TypeRenderer.h"

#include "GUI/Widgets/UITypeRegistry.h"

#include "Scene/Core/Scene.h"

#include <imgui.h>

#include <algorithm>

namespace ya
{

namespace
{

std::string shortTypeName(const std::string& typeId)
{
    const size_t dot = typeId.find_last_of('.');
    return dot == std::string::npos ? typeId : typeId.substr(dot + 1);
}

} // namespace

UIDesignerPanel::UIDesignerPanel(EditorLayer* owner) : _owner(owner)
{
}

UIDesignerPanel::~UIDesignerPanel() = default;

void UIDesignerPanel::openDocument(const std::shared_ptr<UIDocument>& document, const std::string& path)
{
    if (!document) {
        YA_CORE_WARN("UIDesignerPanel::openDocument: null document");
        return;
    }
    _document     = document;
    _documentPath = path;
    _entryScene   = nullptr;
    _entryId.clear();

    _previewTree  = std::make_unique<WidgetTree>(Extent2D{800, 600});
    _previewRoot  = document->instantiate();
    _selected     = nullptr;
    if (!_previewRoot) {
        YA_CORE_ERROR("UIDesignerPanel::openDocument: document '{}' failed to instantiate",
                      document->typeId);
        _document.reset();
        return;
    }
    _previewTree->attachToLayer(WidgetTree::ELayer::Content, _previewRoot);
    _selected = _previewRoot.get();
}

void UIDesignerPanel::openDocumentPath(const std::string& path)
{
    std::string content;
    if (!VirtualFileSystem::get() || !VirtualFileSystem::get()->readFileToString(path, content)) {
        YA_CORE_ERROR("UIDesignerPanel::openDocumentPath: failed to read '{}'", path);
        return;
    }
    try {
        const auto json = nlohmann::json::parse(content);
        auto document   = UIDocument::fromJson(json);
        if (!document) {
            YA_CORE_ERROR("UIDesignerPanel::openDocumentPath: invalid document '{}'", path);
            return;
        }
        openDocument(document, path);
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("UIDesignerPanel::openDocumentPath: parse error in '{}': {}", path, e.what());
    }
}

void UIDesignerPanel::newDocument(const std::string& typeId)
{
    auto document     = std::make_shared<UIDocument>();
    document->typeId  = typeId;
    document->fields  = nlohmann::json::object();
    openDocument(document, {});
}

void UIDesignerPanel::openSceneEntry(Scene& scene, SceneWidgetEntry& entry)
{
    if (!entry.inlineDocument) {
        YA_CORE_WARN("UIDesignerPanel::openSceneEntry: entry '{}' has no inline document", entry.entryId);
        return;
    }
    _entryScene = &scene;
    _entryId    = entry.entryId;
    openDocument(entry.inlineDocument, {});
}

void UIDesignerPanel::rebuildDocumentFromPreview()
{
    if (!_previewRoot) {
        return;
    }
    _document = UIDocument::fromWidget(*_previewRoot);
}

bool UIDesignerPanel::saveDocument()
{
    if (!_document || !_previewRoot) {
        YA_CORE_WARN("UIDesignerPanel::saveDocument: no document open");
        return false;
    }
    rebuildDocumentFromPreview();

    // Scene-entry mode: write the rebuilt document back to the entry.
    if (_entryScene && !_entryId.empty()) {
        for (auto& entry : _entryScene->getWidgetEntries()) {
            if (entry.entryId == _entryId) {
                entry.inlineDocument = _document;
                YA_CORE_INFO("UIDesignerPanel: saved entry '{}' back to scene '{}'",
                             _entryId, _entryScene->getName());
                return true;
            }
        }
        YA_CORE_ERROR("UIDesignerPanel::saveDocument: entry '{}' no longer exists", _entryId);
        return false;
    }

    std::string path = _documentPath;
    if (path.empty()) {
        path = _savePathBuffer;
    }
    if (path.empty()) {
        YA_CORE_WARN("UIDesignerPanel::saveDocument: no save path (use Save As...)");
        return false;
    }
    if (!VirtualFileSystem::get()) {
        YA_CORE_ERROR("UIDesignerPanel::saveDocument: virtual file system unavailable");
        return false;
    }
    VirtualFileSystem::get()->saveToFile(path, _document->toJson().dump(4));
    _documentPath = path;
    YA_CORE_INFO("UIDesignerPanel: saved document to '{}'", path);
    return true;
}

UIFrameSnapshot UIDesignerPanel::buildPreviewSnapshot()
{
    if (!_previewTree) {
        return {};
    }
    return _previewTree->buildSnapshot(UIFrameBuildContext{});
}

UIElement* UIDesignerPanel::pickAt(const glm::vec2& logicalPoint)
{
    return _previewTree ? _previewTree->pickAt(logicalPoint) : nullptr;
}

void UIDesignerPanel::applyPreviewExtent()
{
    if (!_previewTree || !_owner) {
        return;
    }
    const glm::vec2 viewportSize = _owner->getViewportSize();
    if (viewportSize.x > 1.0f && viewportSize.y > 1.0f) {
        _previewTree->setLogicalExtent(Extent2D::fromVec2(viewportSize));
    }
}

void UIDesignerPanel::drawToolbar()
{
    // New: palette-driven root creation.
    if (ImGui::Button("New")) {
        ImGui::OpenPopup("UIDesignerNewType");
    }
    ImGui::SameLine();
    if (ImGui::Button("Open .yaui")) {
        _filePicker.open("Open UI Document", {}, {".yaui"}, [this](const std::string& path) {
            openDocumentPath(path);
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") && hasDocument()) {
        saveDocument();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save As...")) {
        ImGui::OpenPopup("UIDesignerSaveAs");
    }

    if (ImGui::BeginPopup("UIDesignerNewType")) {
        for (const std::string& typeId : UITypeRegistry::instance().getTypeIds()) {
            if (ImGui::MenuItem(typeId.c_str())) {
                newDocument(typeId);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("UIDesignerSaveAs")) {
        ImGui::InputText("Path", _savePathBuffer, sizeof(_savePathBuffer));
        if (ImGui::Button("Save")) {
            if (std::strlen(_savePathBuffer) > 0) {
                _documentPath = _savePathBuffer;
                saveDocument();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }

    if (hasDocument()) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s%s",
                            shortTypeName(_document->typeId).c_str(),
                            _documentPath.empty() ? " (untitled)" : "");
    }
}

void UIDesignerPanel::drawWidgetTree(UIElement& widget)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    const bool bSelected     = _selected == &widget;
    const bool bLeaf         = widget.getChildren().empty();
    if (bLeaf) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (bSelected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const bool bOpened = ImGui::TreeNodeEx(&widget, flags, "%s  [%s]",
                                           widget._name.c_str(),
                                           shortTypeName(widget._typeId).c_str());
    if (ImGui::IsItemClicked()) {
        _selected = &widget;
    }
    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete")) {
            if (WidgetTree* tree = widget.getTree()) {
                tree->detach(widget);
                if (_selected == &widget) {
                    _selected = nullptr;
                }
            }
        }
        ImGui::EndPopup();
    }
    if (bOpened && !bLeaf) {
        for (const auto& child : widget.getChildren()) {
            drawWidgetTree(*child);
        }
        ImGui::TreePop();
    }
}

void UIDesignerPanel::drawPalette()
{
    ImGui::SeparatorText("Palette");
    const bool bHasSelection = _selected != nullptr && _selected->isAttached();
    for (const std::string& typeId : UITypeRegistry::instance().getTypeIds()) {
        ImGui::PushID(typeId.c_str());
        const bool bAdd = ImGui::Button(shortTypeName(typeId).c_str(), ImVec2(-1.0f, 0.0f));
        if (bAdd) {
            UIElementRef widget = UITypeRegistry::instance().createInstance(typeId);
            if (widget && _previewTree) {
                widget->_name = shortTypeName(typeId);
                if (bHasSelection) {
                    _previewTree->attach(*_selected, widget);
                    _selected = widget.get();
                }
                else if (_previewRoot) {
                    // No selection: reparent the root under a new root widget.
                    _previewTree->detach(*_previewRoot);
                    widget->addDetachedChild(_previewRoot);
                    _previewRoot = widget;
                    _previewTree->attachToLayer(WidgetTree::ELayer::Content, _previewRoot);
                    _selected = _previewRoot.get();
                }
                else {
                    newDocument(typeId);
                }
            }
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", typeId.c_str());
        }
        ImGui::PopID();
    }
}

void UIDesignerPanel::drawInspector()
{
    ImGui::SeparatorText("Inspector");
    if (!_selected || !_selected->isAttached()) {
        ImGui::TextDisabled("Select a widget");
        return;
    }

    auto* cls = ClassRegistry::instance().getClass(_selected->getTypeIndex());
    if (!cls) {
        ImGui::TextDisabled("Type not registered: %s", _selected->_typeId.c_str());
        return;
    }

    ya::RenderContext ctx;
    ctx.beginInstance(_selected);
    ya::renderReflectedType(cls->getName(), _selected->getTypeIndex(), _selected, ctx, 0);
}

void UIDesignerPanel::onImGuiRender()
{
    if (!ImGui::Begin("UI Designer")) {
        ImGui::End();
        return;
    }

    drawToolbar();

    if (!hasDocument()) {
        ImGui::TextWrapped("Create a document with New (pick a widget type), open a .yaui "
                           "file, or open a SceneWidgetEntry from the Scene Hierarchy.");
        ImGui::End();
        _filePicker.render();
        return;
    }

    applyPreviewExtent();

    ImGui::SeparatorText("WidgetTree");
    if (_previewRoot) {
        drawWidgetTree(*_previewRoot);
    }
    else {
        ImGui::TextDisabled("Preview root missing");
    }

    drawPalette();
    drawInspector();

    ImGui::End();
    _filePicker.render();
}

} // namespace ya
