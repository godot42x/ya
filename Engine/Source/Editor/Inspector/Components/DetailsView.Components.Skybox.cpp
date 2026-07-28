#include "Editor/Inspector/DetailsViewInternal.h"

namespace ya
{

void DetailsView::drawSkyboxStatus(ESkyboxResolveState resolveState)
{
    const char* label = "Status: Unknown";
    ImVec4      color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

    switch (resolveState) {
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
        const auto entityId = static_cast<entt::entity>(entity);
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
                    _filePicker.openTexturePicker(sc->cubemapSource.files[faceIndex], [sc, faceIndex, entityId](const std::string& newPath) {
                        sc->sourceType                      = ESkyboxSourceType::CubeFaces;
                        sc->cubemapSource.files[faceIndex] = newPath;
                        sc->invalidate();
                        if (auto* resolver = App::get()->getResourceResolveSystem()) {
                            resolver->markSkyboxDirty(entityId, "editor skybox face picked");
                        }
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
                    _filePicker.openTexturePicker(sc->cylindricalSource.filepath, [sc, entityId](const std::string& newPath) {
                        sc->setCylindricalSource(newPath);
                        if (auto* resolver = App::get()->getResourceResolveSystem()) {
                            resolver->markSkyboxDirty(entityId, "editor skybox cylindrical picked");
                        }
                    });
                }
                ImGui::EndTable();
            }
        }

        auto* resolver = App::get()->getResourceResolveSystem();

        if (bSourceChanged) {
            sc->invalidate();
            if (resolver) {
                resolver->markSkyboxDirty(entityId, "editor skybox modified");
            }
        }

        ImGui::Separator();
        drawSkyboxStatus(resolver ? resolver->getSkyboxResolveState(entityId) : ESkyboxResolveState::Empty);
        if (resolver) {
            const auto state = resolver->getSkyboxResolveState(entityId);
            if (state == ESkyboxResolveState::ResolvingSource || state == ESkyboxResolveState::Preprocessing) {
            ImGui::TextDisabled("Waiting for skybox source load or preprocessing to finish");
            }
        }
        if (ImGui::Button("Invalidate##Skybox")) {
            sc->invalidate();
            if (auto* resolver = App::get()->getResourceResolveSystem()) {
                resolver->markSkyboxDirty(entityId, "editor skybox invalidate button");
            }
        }

        drawSkyboxPreviewSection(entity, *sc);
    });
}

void DetailsView::drawSkyboxPreviewSection(const Entity& entity, const SkyboxComponent& skybox)
{
    auto* resolver = App::get()->getResourceResolveSystem();
    auto preview   = resolver ? resolver->getSkyboxPreview(static_cast<entt::entity>(entity)) : SkyboxPreviewInfo{};

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
    const bool bHasAnyFacePreview = std::any_of(
        preview.cubemapFaceViews.begin(),
        preview.cubemapFaceViews.end(),
        [](IImageView* view) { return view != nullptr; });

    if (!preview.bHasRenderableCubemap || !bHasAnyFacePreview) {
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

} // namespace ya
