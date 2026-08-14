#include "Editor/EditorLayerInternal.h"

#include "Gameplay/Systems/Components/DirectionalLightComponent.h"
#include "GameRuntime/GUI/GuiSystem.h"

namespace ya
{
std::vector<RenderOverlayText2D> EditorLayer::buildViewportCameraOverlayTexts() const
{
    if (!_bShowViewportCameraOverlay) {
        return {};
    }

    const glm::quat rotQuat  = glm::quat(glm::radians(_camera._rotation));
    const glm::vec3 forward  = rotQuat * FMath::Vector::WorldForward;
    const glm::vec3 position = _camera.getPosition();

    std::vector<RenderOverlayText2D> texts;
    texts.reserve(2);
    texts.push_back(RenderOverlayText2D{
        .text        = std::format("Pos {:+.2f} {:+.2f} {:+.2f}", position.x, position.y, position.z),
        .viewportPos = {kViewportCameraOverlayMarginX, kViewportCameraOverlayMarginY},
        .color       = {0.92f, 0.92f, 0.92f, 0.92f},
        .fontSize    = 16,
        .depth       = 0.0f,
    });
    texts.push_back(RenderOverlayText2D{
        .text        = std::format("Dir {:+.2f} {:+.2f} {:+.2f}", forward.x, forward.y, forward.z),
        .viewportPos = {kViewportCameraOverlayMarginX, kViewportCameraOverlayMarginY + 18.0f + kViewportCameraOverlayLineSpacing},
        .color       = {0.75f, 0.86f, 1.0f, 0.92f},
        .fontSize    = 16,
        .depth       = 0.0f,
    });
    return texts;
}



bool EditorLayer::screenToViewport(float screenX, float screenY, float& outX, float& outY) const
{
    // Check if point is within viewport bounds
    if (screenX < _viewportBounds[0].x || screenX > _viewportBounds[1].x ||
        screenY < _viewportBounds[0].y || screenY > _viewportBounds[1].y)
    {
        return false;
    }

    // Transform to viewport-local coordinates (0,0 at top-left of viewport)
    outX = screenX - _viewportBounds[0].x;
    outY = screenY - _viewportBounds[0].y;

    return true;
}

bool EditorLayer::screenToViewport(const glm::vec2 in, glm::vec2& out) const
{
    return screenToViewport(in.x, in.y, out.x, out.y);
}

void EditorLayer::viewportWindow()
{
    YA_PROFILE_FUNCTION();
    ya::ImGuiStyleScope style;
    style.pushVar(ImGuiStyleVar_WindowPadding, ImVec2{0, 0});
    style.pushVar(ImGuiStyleVar_WindowMinSize, ImVec2{460, 300});


    if (!ImGui::Begin("Viewport"))
    {
        ImGui::End();
        return;
    }

    // Get viewport panel size
    ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();



    // Calculate viewport bounds for mouse picking
    auto   windowPos = ImGui::GetWindowPos();
    ImVec2 minBound  = ImGui::GetWindowContentRegionMin();
    ImVec2 maxBound  = ImGui::GetWindowContentRegionMax();
    minBound.x += windowPos.x;
    minBound.y += windowPos.y;
    maxBound.x += windowPos.x;
    maxBound.y += windowPos.y;

    _viewportBounds[0] = {minBound.x, minBound.y};
    _viewportBounds[1] = {maxBound.x, maxBound.y};

    // Update viewport size if changed
    const bool bMouseCaptured = _app && _app->getInputRouter().isMouseCaptured();
    if (!bMouseCaptured && (_viewportSize.x != viewportPanelSize.x || _viewportSize.y != viewportPanelSize.y))
    {
        _viewportSize = {viewportPanelSize.x, viewportPanelSize.y};

        // Queue the viewport resize event to be processed in next frame before render
        _pendingViewportRect = Rect2D{
            .pos    = _viewportBounds[0],
            .extent = {viewportPanelSize.x, viewportPanelSize.y},
        };
        _bViewportResizePending = true;
        YA_CORE_INFO("Viewport resize queued: {} x {} (will be processed before render)", _viewportSize.x, _viewportSize.y);
    }

    // Display the render texture from editor render target (unified Texture semantics)
    if (viewportPanelSize.x > 0 && viewportPanelSize.y > 0)
    {
        Sampler* sampler         = _viewPortSamplerType == Linear
                                     ? TextureLibrary::get().getLinearSampler()
                                     : TextureLibrary::get().getNearestSampler();
        auto*    viewportImageView = _viewportDisplayImage && _viewportDisplayImage->getImageView()
                                       ? _viewportDisplayImage->getImageView()
                                       : _viewportCtx.viewportImageView;
        if (viewportImageView) {
            if (isViewportMode2D()) {
                // Canvas navigation is rendered into the compositor output.
                // Keeping this presentation image at full UV range prevents
                // ImGui's texture sampling from stretching the grid or
                // clipping the 2D editor view.
                ImGuiHelper::Image(viewportImageView,
                                   sampler,
                                   "Viewport Texture 2D ",
                                   viewportPanelSize,
                                   ImVec2(0, 0),
                                   ImVec2(1, 1));
            }
            else {
                if (ImGuiHelper::Image(viewportImageView,
                                       sampler,
                                       "Viewport Texture ",
                                       viewportPanelSize,
                                       ImVec2(0, 0),
                                       ImVec2(1, 1)))
                {
                    renderGizmo();
                }
            }
        }
        else {
            ImGui::TextDisabled("Viewport image unavailable");
            ImGui::TextDisabled("Waiting for a valid viewport pass / texture");
        }
    }
    else
    {
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Viewport: %.0f x %.0f", _viewportSize.x, _viewportSize.y);
    }

    if (ImGui::IsMouseClicked(1)) {
        ImGui::SetWindowFocus();
    }


    bViewportFocused = ImGui::IsWindowFocused();
    bViewportHovered = ImGui::IsWindowHovered();

    if (const ImGuiViewport* mainViewport = ImGui::GetMainViewport()) {
        _viewportMouseCenter = {
            (_viewportBounds[0].x + (_viewportBounds[1].x - _viewportBounds[0].x) * 0.5f) - mainViewport->Pos.x,
            (_viewportBounds[0].y + (_viewportBounds[1].y - _viewportBounds[0].y) * 0.5f) - mainViewport->Pos.y,
        };
        _viewportMouseRect = Rect2D{
            .pos = {
                std::round(_viewportBounds[0].x - mainViewport->Pos.x),
                std::round(_viewportBounds[0].y - mainViewport->Pos.y),
            },
            .extent = {
                std::round(_viewportBounds[1].x - _viewportBounds[0].x),
                std::round(_viewportBounds[1].y - _viewportBounds[0].y),
            },
        };
    }

    // Viewport context menu - right-click on blank space
    // Only show if not dragging camera (right mouse drag)
    if (_app && _app->isStopped() && !_bRightMouseDragging)
    {
        ContextMenu ctx("ViewportContextMenu", ContextMenu::Type::BlankSpace);
        if (ctx.begin())
        {
            if (ctx.menuItem("Create Empty Node"))
            {
                if (auto scene = getEditableScene())
                {
                    Node* newNode = scene->createNode3D("New Node");
                    if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                        setSelectedEntity(node3D->getEntity());
                    }
                }
            }

            if (ctx.beginMenu("Create 3D Object"))
            {
                if (ctx.menuItem("Cube"))
                {
                    if (auto scene = getEditableScene())
                    {
                        Node* newNode = scene->createNode3D("Cube");
                        if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                            Entity* newEntity = node3D->getEntity();
                            auto    mc        = newEntity->addComponent<StaticMeshComponent>();
                            mc->setPrimitiveGeometry(EPrimitiveGeometry::Cube);
                            newEntity->addComponent<PhongMaterialComponent>();
                            setSelectedEntity(newEntity);
                        }
                    }
                }
                if (ctx.menuItem("Sphere"))
                {
                    if (auto scene = getEditableScene())
                    {
                        Node* newNode = scene->createNode3D("Sphere");
                        if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                            Entity* newEntity = node3D->getEntity();
                            auto    mc        = newEntity->addComponent<StaticMeshComponent>();
                            mc->setPrimitiveGeometry(EPrimitiveGeometry::Sphere);
                            newEntity->addComponent<PhongMaterialComponent>();
                            setSelectedEntity(newEntity);
                        }
                    }
                }
                if (ctx.menuItem("Plane"))
                {
                    if (auto scene = getEditableScene())
                    {
                        Node* newNode = scene->createNode3D("Plane");
                        if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                            Entity* newEntity = node3D->getEntity();
                            auto    mc        = newEntity->addComponent<StaticMeshComponent>();
                            mc->setPrimitiveGeometry(EPrimitiveGeometry::Quad);
                            newEntity->addComponent<PhongMaterialComponent>();
                            setSelectedEntity(newEntity);
                        }
                    }
                }
                if (ctx.menuItem("Terrain"))
                {
                    if (auto scene = getEditableScene())
                    {
                        Node* newNode = scene->createNode3D("Terrain");
                        if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                            Entity* newEntity = node3D->getEntity();
                            newEntity->addComponent<TerrainComponent>();
                            newEntity->addComponent<PhongMaterialComponent>();
                            setSelectedEntity(newEntity);
                        }
                    }
                }
                ctx.endMenu();
            }

            if (ctx.menuItem("Create Point Light"))
            {
                if (auto scene = getEditableScene())
                {
                    Node* newNode = scene->createNode3D("Point Light");
                    if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                        Entity* newEntity = node3D->getEntity();
                        newEntity->addComponent<PointLightComponent>();
                        setSelectedEntity(newEntity);
                    }
                }
            }

            if (ctx.menuItem("Create Directional Light"))
            {
                if (auto scene = getEditableScene())
                {
                    Node* newNode = scene->createNode3D("Directional Light");
                    if (auto* node3D = dynamic_cast<Node3D*>(newNode)) {
                        Entity* newEntity = node3D->getEntity();
                        newEntity->addComponent<DirectionalLightComponent>();
                        setSelectedEntity(newEntity);
                    }
                }
            }

            ctx.separator();

            // Duplicate / delete the whole selection (primary first).
            const auto& selections = getSelections();
            if (!selections.empty())
            {
                if (ctx.menuItem("Duplicate Selected"))
                {
                    if (auto scene = getEditableScene())
                    {
                        std::vector<Entity*> duplicated;
                        for (Entity* entity : selections) {
                            if (!entity || !entity->isValid()) {
                                continue;
                            }
                            Node* node = scene->getNodeByEntity(entity);
                            if (!node) {
                                continue;
                            }
                            if (auto newNode = scene->duplicateNode(node, node->getParent())) {
                                if (auto* newEntity = newNode->getEntity()) {
                                    duplicated.push_back(newEntity);
                                }
                            }
                        }
                        if (!duplicated.empty()) {
                            YA_CORE_INFO("Duplicated {} entit{}", duplicated.size(), duplicated.size() > 1 ? "ies" : "y");
                            facade().timerManager.delayCall(
                                1,
                                [this, duplicated]() {
                                    _sceneHierarchyPanel.replaceSelection(duplicated, duplicated.front());
                                });
                        }
                    }
                }
                if (ctx.menuItem("Delete Selected"))
                {
                    if (auto scene = getEditableScene())
                    {
                        // Deferred through the hierarchy panel so destruction
                        // never happens while the tree is rendering.
                        _sceneHierarchyPanel.deleteSelection();
                    }
                }
            }

            ctx.end();
        }
    }

    ImGui::End();
}
} // namespace ya
