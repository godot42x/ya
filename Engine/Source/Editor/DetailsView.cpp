#include "DetailsView.h"
#include "Core/Debug/Instrumentor.h"
#include "Core/System/VirtualFileSystem.h"
#include "ECS/Component/2D/UIComponent.h"
#include "ECS/Component/DirectionalLightComponent.h"
#include "ECS/Component/Material/PBRMaterialComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/MeshComponent.h"
#include "ECS/Component/ModelComponent.h"
#include "TypeRenderer.h"



#include "ECS/Component.h"
#include "ECS/Component/2D/BillboardComponent.h"
#include "ECS/Component/3D/EnvironmentLightingComponent.h"
#include "ECS/Component/3D/SkyboxComponent.h"
#include "ECS/Component/LuaScriptComponent.h"
#include "ECS/Component/Material/PhongMaterialComponent.h"
#include "ECS/Component/Material/SimpleMaterialComponent.h"
#include "ECS/Component/Material/UnlitMaterialComponent.h"
#include "ECS/Component/PointLightComponent.h"
#include "ECS/Component/RenderComponent.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/ResourceResolveSystem.h"



#include "EditorLayer.h"
#include "ImGuiHelper.h"
#include "Render/Core/TextureFactory.h"
#include "Resource/TextureLibrary.h"
#include "Runtime/App/App.h"
#include "Scene/Node.h"
#include "Scene/Scene.h"
#include <algorithm>
#include <glm/gtc/type_ptr.hpp>



namespace ya
{

namespace
{

constexpr size_t                                  DETAILS_SCRIPT_INPUT_BUFFER_SIZE = 256;
constexpr size_t                                  DETAILS_SKYBOX_INPUT_BUFFER_SIZE = 512;
constexpr const char*                             SKYBOX_SOURCE_TYPE_LABELS        = "Cube Faces\0Cylindrical\0";
constexpr const char*                             ENVIRONMENT_LIGHTING_SOURCE_TYPE_LABELS = "Scene Skybox\0Cube Faces\0Cylindrical\0";
constexpr float                                   SKYBOX_PREVIEW_MAX_HEIGHT        = 180.0f;
constexpr std::array<const char*, CubeFace_Count> SKYBOX_FACE_LABELS               = {
    "+X",
    "-X",
    "+Y",
    "-Y",
    "+Z",
    "-Z",
};

bool drawPathInput(const char* id, std::string& path, size_t bufferSize)
{
    std::vector<char> buffer(bufferSize, '\0');
    strncpy_s(buffer.data(), buffer.size(), path.c_str(), buffer.size() - 1);
    if (!ImGui::InputText(id, buffer.data(), buffer.size())) {
        return false;
    }

    path = buffer.data();
    return true;
}


void drawTexturePreviewImage(const char* id, Texture* texture, float maxWidth, float maxHeight)
{
    if (!texture || !texture->getImageView()) {
        ImGui::TextDisabled("Preview unavailable");
        return;
    }

    const auto extent = texture->getExtent();
    if (extent.width == 0 || extent.height == 0) {
        ImGui::TextDisabled("Preview unavailable");
        return;
    }

    const float  scale   = std::min(maxWidth / static_cast<float>(extent.width),
                                 maxHeight / static_cast<float>(extent.height));
    const ImVec2 size    = ImVec2(static_cast<float>(extent.width) * scale,
                               static_cast<float>(extent.height) * scale);
    auto         sampler = TextureLibrary::get().getLinearSampler();
    if (!sampler) {
        ImGui::TextDisabled("Preview sampler unavailable");
        return;
    }

    ImGui::PushID(id);
    ImGuiHelper::Image(texture->getImageView(), sampler.get(), "No Preview", size);
    ImGui::PopID();
}

} // namespace

void DetailsView::drawEnvironmentLightingStatus(const EnvironmentLightingComponent& environmentLighting)
{
    const char* label = "Status: Unknown";
    ImVec4      color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

    switch (environmentLighting.resolveState) {
    case EEnvironmentLightingResolveState::Empty:
        label = environmentLighting.usesSceneSkybox() ? "Status: Waiting for scene skybox" : "Status: No environment source";
        break;
    case EEnvironmentLightingResolveState::Dirty:
        label = "Status: Dirty";
        color = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);
        break;
    case EEnvironmentLightingResolveState::ResolvingSource:
        label = "Status: Resolving source";
        color = ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        break;
    case EEnvironmentLightingResolveState::PreprocessingEnvironment:
        label = "Status: Building environment cubemap";
        color = ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        break;
    case EEnvironmentLightingResolveState::PreprocessingIrradiance:
        label = "Status: Building irradiance cubemap";
        color = ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        break;
    case EEnvironmentLightingResolveState::Ready:
        label = "Status: Ready";
        color = ImVec4(0.45f, 1.0f, 0.45f, 1.0f);
        break;
    case EEnvironmentLightingResolveState::Failed:
        label = "Status: Failed";
        color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        break;
    }

    ImGui::TextColored(color, "%s", label);
}

void DetailsView::drawEnvironmentLightingComponent(Entity& entity)
{
    componentWrapper<EnvironmentLightingComponent>("Environment Lighting", entity, [this](EnvironmentLightingComponent* elc) {
        bool bSourceChanged = false;

        int sourceType = static_cast<int>(elc->sourceType);
        if (ImGui::Combo("Source Type##EnvironmentLighting", &sourceType, ENVIRONMENT_LIGHTING_SOURCE_TYPE_LABELS)) {
            elc->sourceType = static_cast<EEnvironmentLightingSourceType>(sourceType);
            bSourceChanged  = true;
        }

        int irradianceFaceSize = static_cast<int>(elc->irradianceFaceSize);
        if (ImGui::DragInt("Irradiance Face Size", &irradianceFaceSize, 1.0f, 4, 256)) {
            elc->irradianceFaceSize = static_cast<uint32_t>(std::max(4, irradianceFaceSize));
            bSourceChanged          = true;
        }

        ImGui::Separator();
        if (elc->sourceType == EEnvironmentLightingSourceType::SceneSkybox) {
            ImGui::TextDisabled("Reuse the scene skybox cubemap as the environment-lighting source.");
        }
        else if (elc->sourceType == EEnvironmentLightingSourceType::CubeFaces) {
            bool flipVertical = elc->cubemapSource.flipVertical;
            if (ImGui::Checkbox("Flip Vertical##EnvironmentLightingCubeFaces", &flipVertical)) {
                elc->cubemapSource.flipVertical = flipVertical;
                bSourceChanged                  = true;
            }

            for (size_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                ImGui::PushID(static_cast<int>(faceIndex));
                ImGui::TextUnformatted(SKYBOX_FACE_LABELS[faceIndex]);
                ImGui::SetNextItemWidth(-90.0f);
                if (drawPathInput("##EnvironmentLightingFacePath", elc->cubemapSource.files[faceIndex], DETAILS_SKYBOX_INPUT_BUFFER_SIZE)) {
                    bSourceChanged = true;
                }

                ImGui::SameLine();
                if (ImGui::Button("Browse##EnvironmentLightingFace")) {
                    _filePicker.openTexturePicker(elc->cubemapSource.files[faceIndex], [elc, faceIndex](const std::string& newPath) {
                        elc->sourceType                      = EEnvironmentLightingSourceType::CubeFaces;
                        elc->cubemapSource.files[faceIndex] = newPath;
                        elc->invalidate();
                    });
                }
                ImGui::PopID();
            }
        }
        else {
            bool flipVertical = elc->cylindricalSource.flipVertical;
            if (ImGui::Checkbox("Flip Vertical##EnvironmentLightingCylindrical", &flipVertical)) {
                elc->cylindricalSource.flipVertical = flipVertical;
                bSourceChanged                      = true;
            }

            if (ImGui::BeginTable("EnvironmentLightingCylindricalPathTable", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Browse", ImGuiTableColumnFlags_WidthFixed, 88.0f);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                if (drawPathInput("Cylindrical Source##EnvironmentLighting", elc->cylindricalSource.filepath, DETAILS_SKYBOX_INPUT_BUFFER_SIZE)) {
                    bSourceChanged = true;
                }

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("Browse##EnvironmentLightingCylindrical", ImVec2(-1.0f, 0.0f))) {
                    _filePicker.openTexturePicker(elc->cylindricalSource.filepath, [elc](const std::string& newPath) {
                        elc->setCylindricalSource(newPath);
                    });
                }
                ImGui::EndTable();
            }
        }

        if (bSourceChanged) {
            elc->invalidate();
        }

        ImGui::Separator();
        drawEnvironmentLightingStatus(*elc);
        if (elc->isLoading()) {
            ImGui::TextDisabled("Waiting for environment-lighting resolve to finish");
        }
        if (ImGui::Button("Invalidate##EnvironmentLighting")) {
            elc->invalidate();
        }
    });
}

void DetailsView::drawSkyboxStatus(const SkyboxComponent& skybox)
{
    const char* label = "Status: Unknown";
    ImVec4      color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

    switch (skybox.resolveState) {
    case ESkyboxResolveState::Empty:
        label = "Status: No skybox source";
        color = ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        break;
    case ESkyboxResolveState::Dirty:
        label = "Status: Dirty";
        color = ImVec4(1.0f, 0.75f, 0.25f, 1.0f);
        break;
    case ESkyboxResolveState::ResolvingSource:
        label = "Status: Loading Source...";
        color = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
        break;
    case ESkyboxResolveState::Preprocessing:
        label = "Status: Preprocessing...";
        color = ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
        break;
    case ESkyboxResolveState::Ready:
        label = "Status: Ready";
        color = ImVec4(0.2f, 0.9f, 0.3f, 1.0f);
        break;
    case ESkyboxResolveState::Failed:
        label = "Status: Failed";
        color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        break;
    }

    ImGui::TextColored(color, "%s", label);
}

void DetailsView::drawSkyboxComponent(Entity& entity)
{
    componentWrapper<SkyboxComponent>("Skybox", entity, [this, &entity](SkyboxComponent* sc) {
        bool bSourceChanged = false;

        int sourceType = static_cast<int>(sc->sourceType);
        if (ImGui::Combo("Source Type", &sourceType, SKYBOX_SOURCE_TYPE_LABELS)) {
            sc->sourceType = static_cast<ESkyboxSourceType>(sourceType);
            bSourceChanged = true;
        }

        ImGui::Separator();
        if (sc->sourceType == ESkyboxSourceType::CubeFaces) {
            ImGui::TextDisabled("Use six textures to author the cubemap directly.");

            bool flipVertical = sc->cubemapSource.flipVertical;
            if (ImGui::Checkbox("Flip Vertical", &flipVertical)) {
                sc->cubemapSource.flipVertical = flipVertical;
                bSourceChanged                 = true;
            }

            for (size_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
                ImGui::PushID(static_cast<int>(faceIndex));
                ImGui::TextUnformatted(SKYBOX_FACE_LABELS[faceIndex]);
                ImGui::SetNextItemWidth(-90.0f);
                if (drawPathInput("##SkyboxFacePath", sc->cubemapSource.files[faceIndex], DETAILS_SKYBOX_INPUT_BUFFER_SIZE)) {
                    bSourceChanged = true;
                }

                ImGui::SameLine();
                if (ImGui::Button("Browse")) {
                    _filePicker.openTexturePicker(sc->cubemapSource.files[faceIndex], [sc, faceIndex](const std::string& newPath) {
                        sc->sourceType                     = ESkyboxSourceType::CubeFaces;
                        sc->cubemapSource.files[faceIndex] = newPath;
                        sc->invalidate();
                    });
                }
                ImGui::PopID();
            }
        }
        else {
            ImGui::TextDisabled("Use one cylindrical/equirectangular texture. It will be converted offscreen to a cubemap.");

            bool flipVertical = sc->cylindricalSource.flipVertical;
            if (ImGui::Checkbox("Flip Vertical##SkyboxCylindrical", &flipVertical)) {
                sc->cylindricalSource.flipVertical = flipVertical;
                bSourceChanged                     = true;
            }

            if (ImGui::BeginTable("SkyboxCylindricalPathTable", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Browse", ImGuiTableColumnFlags_WidthFixed, 88.0f);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::SetNextItemWidth(-1.0f);
                if (drawPathInput("Cylindrical Texture", sc->cylindricalSource.filepath, DETAILS_SKYBOX_INPUT_BUFFER_SIZE)) {
                    bSourceChanged = true;
                }

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Button("Browse##SkyboxCylindrical", ImVec2(-1.0f, 0.0f))) {
                    _filePicker.openTexturePicker(sc->cylindricalSource.filepath, [sc](const std::string& newPath) {
                        sc->setCylindricalSource(newPath);
                    });
                }
                ImGui::EndTable();
            }
        }

        if (bSourceChanged) {
            sc->invalidate();
        }

        ImGui::Separator();
        drawSkyboxStatus(*sc);
        if (sc->isLoading()) {
            ImGui::TextDisabled("Waiting for skybox source load or preprocessing to finish");
        }
        if (ImGui::Button("Invalidate##Skybox")) {
            sc->invalidate();
        }

        drawSkyboxPreviewSection(entity, *sc);
    });
}

void DetailsView::drawSkyboxPreviewSection(const Entity& entity, const SkyboxComponent& skybox)
{
    auto* resolver = App::get()->getResourceResolveSystem();
    auto preview = resolver ? resolver->getSkyboxPreview(static_cast<entt::entity>(entity)) : SkyboxPreviewInfo{};

    ImGui::Separator();
    ImGui::TextUnformatted("Preview");
    drawSkyboxSourcePreview(preview, skybox);
    drawSkyboxCubemapPreviewGrid(preview);
}

void DetailsView::drawSkyboxSourcePreview(const SkyboxPreviewInfo& preview, const SkyboxComponent& skybox)
{
    if (skybox.sourceType != ESkyboxSourceType::Cylindrical) {
        return;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Source Image");
    auto* sourceTexture = preview.sourcePreviewTexture;
    if (!sourceTexture || !sourceTexture->getImageView()) {
        ImGui::TextDisabled("Source preview unavailable until the texture is loaded.");
        return;
    }

    const float previewWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x);
    drawTexturePreviewImage("SkyboxSourcePreview", sourceTexture, previewWidth, SKYBOX_PREVIEW_MAX_HEIGHT);
}

void DetailsView::drawSkyboxCubemapPreviewGrid(const SkyboxPreviewInfo& preview)
{
    if (!preview.bHasRenderableCubemap || !preview.cubemapTexture ||
        !preview.cubemapTexture->getImageShared() || !preview.cubemapTexture->getImageView()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Cubemap face previews unavailable until preprocessing completes.");
        return;
    }

    auto sampler = TextureLibrary::get().getLinearSampler();
    if (!sampler) {
        ImGui::Spacing();
        ImGui::TextDisabled("Cubemap face previews unavailable.");
        return;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Cubemap Faces");
    const float availableWidth = std::max(220.0f, ImGui::GetContentRegionAvail().x);
    const float spacing        = ImGui::GetStyle().ItemSpacing.x;
    const float cellWidth      = std::max(96.0f, (availableWidth - spacing * 2.0f) / 3.0f);
    const float cellHeight     = cellWidth;

    if (!ImGui::BeginTable("SkyboxFacePreviewTable", 3, ImGuiTableFlags_SizingFixedFit)) {
        return;
    }

    for (uint32_t faceIndex = 0; faceIndex < CubeFace_Count; ++faceIndex) {
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(faceIndex));
        ImGui::TextUnformatted(SKYBOX_FACE_LABELS[faceIndex]);

        if (preview.cubemapFaceViews[faceIndex]) {
            ImGuiHelper::Image(preview.cubemapFaceViews[faceIndex], sampler.get(), "No Preview", ImVec2(cellWidth, cellHeight));
        }
        else {
            ImGui::Dummy(ImVec2(cellWidth, cellHeight));
            ImGui::TextDisabled("No Preview");
        }
        ImGui::PopID();
    }

    ImGui::EndTable();
}

DetailsView::DetailsView(EditorLayer* owner) : _owner(owner)
{
}

void DetailsView::onImGuiRender()
{
    YA_PROFILE_FUNCTION();
    ImGui::SetNextWindowSize(ImVec2(300, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Properties"))
    {
        ImGui::End();
        return;
    }

    if (!_owner->getSelections().empty())
    {
        if (auto* firstEntity = _owner->getSelections()[0]; firstEntity->isValid()) {

            drawComponents(*firstEntity);
        }
    }

    ImGui::End();

    // 渲染文件选择器弹窗
    _filePicker.render();
}



// MARK: drawComponents
void DetailsView::drawComponents(Entity& entity)
{
    YA_PROFILE_FUNCTION();
    if (!entity)
    {
        return;
    }

    ImGui::Text("Entity ID: %u", entity.getId());
    ImGui::SameLine();
    drawAddComponentButton(entity);
    ImGui::Separator();

    // ★ 自定义名字编辑器：优先使用 Node 的名字
    Scene* scene = entity.getScene();
    Node*  node  = scene ? scene->getNodeByEntity(&entity) : nullptr;

    {
        ImGuiStyleScope style;
        style.pushColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.5f, 1.0f));
        ImGui::PushID("Name");
        if (node) {
            // 使用 Node 的名字
            char        buffer[256];
            std::string name = node->getName();
            strncpy_s(buffer, name.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';

            if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                node->setName(buffer);
            }
        }
        else {
            // Fallback: 使用 Entity::name（兼容 flat entity）
            char buffer[256];
            strncpy_s(buffer, entity.name.c_str(), sizeof(buffer) - 1);
            buffer[sizeof(buffer) - 1] = '\0';

            if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
                entity.name = buffer;
            }
        }
        ImGui::PopID();
    }
    std::set<ya::type_index_t> hasDraw;

    // TODO: support function reflection
    // for (const auto& [name, ti] : ECSRegistry::get().getTypeIndexCache()) {
    //     if (hasDraw.contains(ti)) {
    //         continue;
    //     }
    //     auto component = ECSRegistry::get().getComponent(ti, scene->getRegistry(), entity);
    //     if (!component) {
    //         continue;
    //     }

    //     ya::RenderContext ctx;
    //     ctx.beginInstance(component);
    //     ya::renderReflectedType(name, ti, component, ctx, 0);
    //     bool bComponentDirty = ctx.hasModifications();
    //     if constexpr (std::is_invocable_v<decltype(onComponentDirty), T*>) {
    //         if (bComponentDirty) {
    //             onComponentDirty(component);
    //         }
    //     }
    // }

    drawReflectedComponent<TransformComponent>("Transform", entity, [](TransformComponent* tc) {
        // Mark both local and world as dirty, then propagate to children
        tc->markLocalDirty();
        tc->propagateWorldDirtyToChildren();
    });
    drawReflectedComponent<ModelComponent>("Model", entity, [](ModelComponent* mc) {
        mc->invalidate();
    });
    drawReflectedComponent<MeshComponent>("Mesh", entity, [](MeshComponent* mc) {
        mc->invalidate();
    });
    drawReflectedComponent<UIComponent>("UI Component", entity, [](UIComponent* uc) {});
    drawReflectedComponent<DirectionalLightComponent>("Directional Light", entity, [](DirectionalLightComponent* dlc) {});

    // 其他组件保持原有的自定义渲染逻辑
    drawComponent<SimpleMaterialComponent>("Simple Material", entity, [](SimpleMaterialComponent* smc) {
        auto* simpleMat = smc->getMaterial();
        if (!simpleMat) {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Material not resolved");
            return;
        }
        int colorType = static_cast<int>(simpleMat->colorType);
        if (ImGui::Combo("Color Type", &colorType, "Normal\0Texcoord\0\0")) {
            simpleMat->colorType = static_cast<SimpleMaterial::EColor>(colorType);
        }
    });

    drawReflectedComponent<RenderComponent>("Render Component", entity, [](RenderComponent* rc) {
        // TODO: implement render component details
    });
    drawReflectedComponent<BillboardComponent>("Billboard", entity, [](BillboardComponent* bc) {
        bc->invalidate();
    });
    drawSkyboxComponent(entity);
    drawEnvironmentLightingComponent(entity);

    drawReflectedComponent<UnlitMaterialComponent>("Unlit Material", entity, [](UnlitMaterialComponent* umc, const ya::RenderContext& ctx) {
        if (ctx.hasModifications()) {
            umc->onEditorPropertiesChanged(ctx.getModificationPaths());
        }
        if (ImGui::Button("Invalidate")) {
            umc->invalidate();
        }
    });

    drawReflectedComponent<PhongMaterialComponent>("Phong Material", entity, [](PhongMaterialComponent* pmc, const ya::RenderContext& ctx) {
        if (ctx.hasModifications()) {
            pmc->onEditorPropertiesChanged(ctx.getModificationPaths());
        }
        if (ImGui::Button("Invalidate")) {
            pmc->invalidate();
        }
    });

    drawReflectedComponent<PBRMaterialComponent>("PBR Material", entity, [](PBRMaterialComponent* pmc, const ya::RenderContext& ctx) {
        if (ctx.hasModifications()) {
            pmc->onEditorPropertiesChanged(ctx.getModificationPaths());
        }
        if (ImGui::Button("Invalidate##PBR")) {
            pmc->invalidate();
        }
    });

    drawReflectedComponent<PointLightComponent>("Point Light", entity, [](PointLightComponent* plc) {
        // TODO: implement point light component details
    });

    drawComponent<LuaScriptComponent>("Lua Script", entity, [this](LuaScriptComponent* lsc) {
        // Add new script button
        if (ImGui::Button("+ Add Script")) {
            _filePicker.openScriptPicker(
                "",
                [lsc](const std::string& scriptPath) {
                    lsc->addScript(scriptPath);
                });
        }

        ImGui::Separator();

        // List all scripts
        size_t indexToRemove = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < lsc->scripts.size(); ++i) {
            auto& script = lsc->scripts[i];

            ImGui::PushID(static_cast<int>(i));

            // Script header with enable/disable checkbox
            bool headerOpen = ImGui::CollapsingHeader(
                script.scriptPath.empty() ? "[Empty Script]" : script.scriptPath.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen);

            ImGui::Checkbox("Enabled##enabled", &script.enabled);

            if (headerOpen) {
                ImGui::Indent();

                // Script path input
                char buffer[DETAILS_SCRIPT_INPUT_BUFFER_SIZE];
                memset(buffer, 0, sizeof(buffer));
                strncpy(buffer, script.scriptPath.c_str(), sizeof(buffer) - 1);

                ImGui::SetNextItemWidth(-80); // 留出按钮空间
                if (ImGui::InputText("##ScriptPath", buffer, sizeof(buffer))) {
                    script.scriptPath              = std::string(buffer);
                    script.bLoaded                 = false; // Force reload
                    script.bEditorPreviewAttempted = false; // 重置编辑器预览标记
                }

                ImGui::SameLine();
                if (ImGui::Button("Browse...")) {
                    _filePicker.openScriptPicker(script.scriptPath, [&script](const std::string& newPath) {
                        script.scriptPath              = newPath;
                        script.bLoaded                 = false;
                        script.bEditorPreviewAttempted = false;
                    });
                }

                // Status and auto-preview
                bool hasValidPath  = !script.scriptPath.empty();
                bool hasProperties = script.self.valid() && !script.properties.empty();

                if (script.bLoaded) {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Status: Loaded (Runtime)");
                }
                else if (hasProperties) {
                    ImGui::TextColored(ImVec4(0.3f, 0.7f, 1.0f, 1), "Status: Preview Mode (Editor)");
                }
                else {
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "Status: Not Loaded");
                }

                // Auto-preview on first expand (if not runtime-loaded)
                if (hasValidPath && !script.bLoaded && !script.bEditorPreviewAttempted) {
                    tryLoadScriptForEditor(&script);
                }

                // 显示脚本属性
                if (script.self.valid()) {
                    ImGui::Separator();

                    // 刷新属性列表（首次或需要时）
                    if (script.properties.empty()) {
                        script.refreshProperties();
                    }

                    if (!script.properties.empty()) {
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Script Properties:");

                        // 渲染每个属性
                        for (auto& prop : script.properties) {
                            renderScriptProperty(&prop, &script);
                        }

                        // 手动刷新按钮
                        if (ImGui::Button("Refresh Properties")) {
                            script.refreshProperties();
                        }
                    }
                    else {
                        ImGui::TextDisabled("No properties found");
                        ImGui::TextDisabled("Tip: Use _PROPERTIES table to define editable properties");
                    }
                }
                else if (hasValidPath) {
                    // 显示加载失败信息
                    ImGui::Separator();
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "Failed to load script");
                    ImGui::TextDisabled("Check console for error details");
                    if (ImGui::Button("Retry Load")) {
                        script.bEditorPreviewAttempted = false; // 重置标记
                        tryLoadScriptForEditor(&script);
                    }
                }

                ImGui::Separator();

                // Remove button
                if (ImGui::Button("Remove Script")) {
                    indexToRemove = i;
                }

                ImGui::Unindent();
            }

            ImGui::PopID();
            ImGui::Separator();
        }

        // Remove marked script
        if (indexToRemove != std::numeric_limits<size_t>::max()) {
            auto eraseIt = lsc->scripts.begin() + static_cast<std::ptrdiff_t>(indexToRemove);
            lsc->scripts.erase(eraseIt);
        }
    });
}

void DetailsView::drawAddComponentButton(Entity& entity)
{
    // ImGui::Separator();

    // Center the button
    float buttonWidth = 200.0f;
    float windowWidth = ImGui::GetContentRegionAvail().x;
    float cursorPosX  = (windowWidth - buttonWidth) * 0.5f;
    if (cursorPosX > 0) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cursorPosX);
    }

    if (ImGui::Button("Add Component", ImVec2(buttonWidth, 0))) {
        ImGui::OpenPopup("AddComponentPopup");
    }

    if (ImGui::BeginPopup("AddComponentPopup")) {
        // Get all registered components from ECSRegistry
        auto& ecsRegistry = ECSRegistry::get();

        // Filter input
        static char searchFilter[128] = "";
        ImGui::InputTextWithHint("##ComponentSearch", "Search...", searchFilter, sizeof(searchFilter));
        ImGui::Separator();

        std::string filterStr = searchFilter;
        std::ranges::transform(filterStr, filterStr.begin(), ::tolower);

        // static std::

        for (auto& [fname, typeIndex] : ecsRegistry.getTypeIndexCache()) {
            std::string componentName = fname.toString();

            // Apply search filter
            if (!filterStr.empty()) {
                std::string lowerName = componentName;
                std::ranges::transform(lowerName, lowerName.begin(), ::tolower);
                if (lowerName.find(filterStr) == std::string::npos) {
                    continue;
                }
            }

            auto* scene = entity.getScene();
            if (ecsRegistry.hasComponent(typeIndex, *scene, entity.getHandle())) {
                ImGui::BeginDisabled();
                ImGui::MenuItem(componentName.c_str());
                ImGui::EndDisabled();
            }
            else {
                if (ImGui::MenuItem(componentName.c_str())) {
                    // Create the component
                    if (void* compPtr = ecsRegistry.addComponent(typeIndex, *scene, entity.getHandle())) {
                        YA_CORE_INFO("Added component '{}' to entity '{}' {}", componentName, entity.getName(), compPtr);
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
        }

        ImGui::EndPopup();
    }
}

void DetailsView::renderScriptProperty(void* propPtr, void* scriptInstancePtr)
{
    using namespace ImGui;

    auto&       prop        = *static_cast<LuaScriptComponent::ScriptProperty*>(propPtr);
    auto&       script      = *static_cast<LuaScriptComponent::ScriptInstance*>(scriptInstancePtr);
    sol::table& scriptTable = script.self;

    // 显示 tooltip（如果有）
    if (!prop.tooltip.empty()) {
        TextDisabled("(?)");
        if (IsItemHovered()) {
            SetTooltip("%s", prop.tooltip.c_str());
        }
        SameLine();
    }

    bool     bModified = false;
    std::any anyValue; // 直接存储类型化的值

    if (prop.typeHint == "float")
    {
        float value = prop.value.as<float>();
        if (DragFloat(prop.name.c_str(), &value, 0.1f, prop.min, prop.max))
        {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "int")
    {
        int value = static_cast<int>(prop.value.as<float>());
        if (DragInt(prop.name.c_str(), &value, 1.0f, (int)prop.min, (int)prop.max))
        {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "bool")
    {
        bool value = prop.value.as<bool>();
        if (Checkbox(prop.name.c_str(), &value))
        {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "string")
    {
        std::string value = prop.value.as<std::string>();
        char        buffer[DETAILS_SCRIPT_INPUT_BUFFER_SIZE];
        memset(buffer, 0, sizeof(buffer));
        strncpy(buffer, value.c_str(), sizeof(buffer) - 1);
        if (InputText(prop.name.c_str(), buffer, sizeof(buffer)))
        {
            std::string newValue   = buffer;
            scriptTable[prop.name] = newValue;
            prop.value             = sol::make_object(scriptTable.lua_state(), newValue);
            anyValue               = newValue;
            bModified              = true;
        }
    }
    else if (prop.typeHint == "Vec3")
    {
        glm::vec3 value = prop.value.as<glm::vec3>();
        if (DragFloat3(prop.name.c_str(), &value.x, 0.1f))
        {
            scriptTable[prop.name] = value;
            prop.value             = sol::make_object(scriptTable.lua_state(), value);
            anyValue               = value;
            bModified              = true;
        }
    }
    else
    {
        // 未知类型，只显示不可编辑
        TextDisabled("%s: [%s]", prop.name.c_str(), prop.typeHint.c_str());
    }

    // 如果属性被修改，直接保存 std::any 到 propertyOverrides
    if (bModified)
    {
        script.propertyOverrides[prop.name] = anyValue;
        YA_CORE_TRACE("[Editor] Property '{}' modified (type: {})", prop.name, anyValue.type().name());
    }
}

void DetailsView::tryLoadScriptForEditor(void* scriptPtr)
{
    using namespace ya;

    auto& script = *static_cast<LuaScriptComponent::ScriptInstance*>(scriptPtr);

    // 初始化编辑器 Lua 状态（仅一次）
    if (!_editorLuaInitialized) {
        YA_CORE_INFO("Initializing editor Lua state for property preview...");

        _editorLua.open_libraries(sol::lib::base,
                                  sol::lib::package,
                                  sol::lib::math,
                                  sol::lib::string,
                                  sol::lib::table);

        // 设置全局环境标识
        _editorLua["IS_EDITOR"]  = true;
        _editorLua["IS_RUNTIME"] = false;

        // 配置 Lua 模块搜索路径（与运行时保持一致）
        _editorLua.script(R"(
            package.path = package.path .. ';./Engine/Content/Lua/?.lua'
            package.path = package.path .. ';./Engine/Content/Lua/?/init.lua'
            package.path = package.path .. ';./Content/Scripts/?.lua'
            package.path = package.path .. ';./Content/Scripts/?/init.lua'
            print('[Editor Lua] Package paths: ' .. package.path)
        )");

        // 注册必要的类型
        _editorLua.new_usertype<glm::vec3>("Vec3",
                                           sol::constructors<glm::vec3(), glm::vec3(float), glm::vec3(float, float, float)>(),
                                           "x",
                                           &glm::vec3::x,
                                           "y",
                                           &glm::vec3::y,
                                           "z",
                                           &glm::vec3::z,
                                           "new",
                                           sol::factories(
                                               []() { return glm::vec3(0.0f); },
                                               [](float x, float y, float z) { return glm::vec3(x, y, z); }));

        _editorLuaInitialized = true;
        YA_CORE_INFO("Editor Lua state initialized");
    }

    // 加载脚本文件
    std::string scriptContent;
    if (!VirtualFileSystem::get()->readFileToString(script.scriptPath, scriptContent)) {
        YA_CORE_ERROR("[Editor Preview] Failed to read file: {}", script.scriptPath);
        // 文件读取失败时清空旧数据，不再重试（直到路径被修改）
        script.bEditorPreviewAttempted = true;
        script.self                    = sol::lua_nil;
        script.properties.clear();
        return;
    }

    YA_CORE_INFO("[Editor Preview] Loading script: {}", script.scriptPath);

    // 标记已尝试加载（防止重复）
    script.bEditorPreviewAttempted = true;

    try {
        // 在编辑器 Lua 状态中执行
        sol::load_result loadResult = _editorLua.load(scriptContent);
        if (!loadResult.valid()) {
            sol::error err = loadResult;
            YA_CORE_ERROR("[Editor Preview] Lua syntax error in {}: {}", script.scriptPath, err.what());
            script.self = sol::lua_nil;
            script.properties.clear();
            return;
        }

        sol::protected_function_result result = loadResult();
        if (!result.valid()) {
            sol::error err = result;
            YA_CORE_ERROR("[Editor Preview] Lua execution error in {}: {}", script.scriptPath, err.what());
            script.self = sol::lua_nil;
            script.properties.clear();
            return;
        }

        if (result.get_type() != sol::type::table) {
            YA_CORE_ERROR("[Editor Preview] Script {} must return a table", script.scriptPath);
            script.self = sol::lua_nil;
            script.properties.clear();
            return;
        }

        sol::table scriptTable = result;
        script.self            = scriptTable;

        // 刷新属性（不调用 onInit）
        script.refreshProperties();

        YA_CORE_INFO("[Editor Preview] Successfully loaded script: {} ({} properties)",
                     script.scriptPath,
                     script.properties.size());
    }
    catch (const sol::error& e) {
        YA_CORE_ERROR("[Editor Preview] Exception while loading {}: {}", script.scriptPath, e.what());
        script.self = sol::lua_nil;
        script.properties.clear();
    }
    catch (const std::exception& e) {
        YA_CORE_ERROR("[Editor Preview] Unexpected error: {}", e.what());
        script.self = sol::lua_nil;
        script.properties.clear();
    }
}

void DetailsView::testNewRenderInterface(Entity& entity)
{
    // 示例：使用新的renderReflectedType接口
    if (auto* transform = entity.getComponent<TransformComponent>()) {
        ya::RenderContext ctx;
        ctx.beginInstance(transform); // Set current instance for PropertyId generation
        ya::renderReflectedType("Transform", ya::type_index_v<TransformComponent>, transform, ctx, 0);

        // 新的API用法：高效查询特定属性是否被修改
        if (ctx.isModified("_position")) {
            YA_CORE_INFO("Position was modified!");
        }
        if (ctx.isModifiedPrefix("_rotation")) {
            YA_CORE_INFO("Some rotation property was modified!");
        }

        // 遍历所有修改
        if (ctx.hasModifications()) {
            for (const auto& mod : ctx.modifications) {
                YA_CORE_INFO("Property {} was modified (path: {})", mod.propPath, mod.propId.id);
            }
        }
    }
}

} // namespace ya
