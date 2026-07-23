#include "Editor/Inspector/DetailsViewInternal.h"

namespace ya
{

void DetailsView::drawEnvironmentLightingStatus(const EnvironmentLightingComponent& environmentLighting)
{
    auto drawSourceBranchState = [](const char* branchLabel,
                                    EEnvironmentLightingSourceResolveState state,
                                    const char* emptyLabel = "Empty") {
        const char* label = emptyLabel;
        ImVec4      color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

        switch (state) {
        case EEnvironmentLightingSourceResolveState::Empty: {
            label = emptyLabel;
        } break;
        case EEnvironmentLightingSourceResolveState::Dirty: {
            label = "Dirty";
            color = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);
        } break;
        case EEnvironmentLightingSourceResolveState::ResolvingSource: {
            label = "Resolving source";
            color = ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        } break;
        case EEnvironmentLightingSourceResolveState::BuildingEnvironmentCubemap: {
            label = "Building environment cubemap";
            color = ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        } break;
        case EEnvironmentLightingSourceResolveState::Ready: {
            label = "Ready";
            color = ImVec4(0.45f, 1.0f, 0.45f, 1.0f);
        } break;
        case EEnvironmentLightingSourceResolveState::Failed: {
            label = "Failed";
            color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        } break;
        }

        ImGui::TextUnformatted(branchLabel);
        ImGui::SameLine();
        ImGui::TextColored(color, "%s", label);
    };

    auto drawDerivedBranchState = [](const char* branchLabel,
                                     auto state,
                                     const char* buildingLabel,
                                     const char* emptyLabel = "Empty") {
        const char* label = emptyLabel;
        ImVec4      color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);

        switch (state) {
        case decltype(state)::Empty: {
            label = emptyLabel;
        } break;
        case decltype(state)::Disabled: {
            label = "Disabled";
            color = ImVec4(0.65f, 0.65f, 0.65f, 1.0f);
        } break;
        case decltype(state)::Dirty: {
            label = "Dirty";
            color = ImVec4(1.0f, 0.85f, 0.35f, 1.0f);
        } break;
        case decltype(state)::Building: {
            label = buildingLabel;
            color = ImVec4(0.45f, 0.75f, 1.0f, 1.0f);
        } break;
        case decltype(state)::Ready: {
            label = "Ready";
            color = ImVec4(0.45f, 1.0f, 0.45f, 1.0f);
        } break;
        case decltype(state)::Failed: {
            label = "Failed";
            color = ImVec4(1.0f, 0.35f, 0.35f, 1.0f);
        } break;
        }

        ImGui::TextUnformatted(branchLabel);
        ImGui::SameLine();
        ImGui::TextColored(color, "%s", label);
    };

    drawSourceBranchState("Source:",
                          environmentLighting.sourceState,
                          environmentLighting.usesSceneSkybox() ? "Waiting for scene skybox" : "No environment source");
    drawDerivedBranchState("Irradiance:",
                           environmentLighting.irradianceState,
                           "Building irradiance map",
                           "Waiting for source");
    drawDerivedBranchState("Prefilter:",
                           environmentLighting.prefilterState,
                           "Building prefilter map",
                           "Waiting for source");
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

        bool bEnableIrradiance = elc->bEnableIrradiance;
        if (ImGui::Checkbox("Enable Irradiance", &bEnableIrradiance)) {
            elc->bEnableIrradiance = bEnableIrradiance;
            bSourceChanged         = true;
        }

        bool bEnablePrefilter = elc->bEnablePrefilter;
        if (ImGui::Checkbox("Enable Prefilter", &bEnablePrefilter)) {
            elc->bEnablePrefilter = bEnablePrefilter;
            bSourceChanged        = true;
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
        if (elc->bEnablePrefilter) {
            ImGui::TextDisabled("Prefilter cubemap will appear in DebugWindow after resolve completes.");
        }
        if (elc->isLoading()) {
            ImGui::TextDisabled("Waiting for environment-lighting resolve to finish");
        }
        if (ImGui::Button("Invalidate##EnvironmentLighting")) {
            elc->invalidate();
        }
    });
}

} // namespace ya
