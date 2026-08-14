#include "GameEditor/ImGui/ImGuiHelper.h"

namespace ya::ImGuiHelper
{

ComponentMapping BuildRGBAChannelMaskMapping(const std::array<bool, 4>& channelEnabled)
{
    const bool bR = channelEnabled[0];
    const bool bG = channelEnabled[1];
    const bool bB = channelEnabled[2];
    const bool bA = channelEnabled[3];

    auto chooseColor = [bR, bG, bB, bA]() -> EComponentSwizzle::T
    {
        if (bR) return EComponentSwizzle::R;
        if (bG) return EComponentSwizzle::G;
        if (bB) return EComponentSwizzle::B;
        if (bA) return EComponentSwizzle::A;
        return EComponentSwizzle::Zero;
    };

    const EComponentSwizzle::T fallback = chooseColor();
    return ComponentMapping{
        .r = bR ? EComponentSwizzle::R : fallback,
        .g = bG ? EComponentSwizzle::G : fallback,
        .b = bB ? EComponentSwizzle::B : fallback,
        .a = bA ? EComponentSwizzle::A : EComponentSwizzle::One,
    };
}

bool IsIdentityRGBAChannelMask(const std::array<bool, 4>& channelEnabled)
{
    return channelEnabled[0] && channelEnabled[1] && channelEnabled[2] && channelEnabled[3];
}

bool RenderRGBAChannelMaskButtons(std::array<bool, 4>& channelEnabled, float frameRounding)
{
    static constexpr const char* kChannelLabels[] = {"R", "G", "B", "A"};

    float totalSpacing = ImGui::GetStyle().ItemSpacing.x * static_cast<float>(IM_ARRAYSIZE(kChannelLabels) - 1);
    float buttonWidth  = (ImGui::GetContentRegionAvail().x - totalSpacing) / static_cast<float>(IM_ARRAYSIZE(kChannelLabels));
    bool  maskChanged  = false;

    ya::ImGuiStyleScope maskStyle;
    maskStyle.pushVar(ImGuiStyleVar_FrameRounding, frameRounding);

    for (int i = 0; i < IM_ARRAYSIZE(kChannelLabels); ++i) {
        const bool bSelected   = channelEnabled[i];
        ImVec4     buttonColor = bSelected ? ImVec4(0.22f, 0.58f, 0.98f, 0.95f) : ImVec4(0.18f, 0.20f, 0.24f, 0.85f);
        ImVec4     hoverColor  = bSelected ? ImVec4(0.30f, 0.66f, 1.00f, 1.00f) : ImVec4(0.24f, 0.27f, 0.32f, 0.95f);
        ImVec4     activeColor = bSelected ? ImVec4(0.16f, 0.48f, 0.88f, 1.00f) : ImVec4(0.20f, 0.23f, 0.28f, 1.00f);

        ya::ImGuiStyleScope buttonStyle;
        buttonStyle.pushColor(ImGuiCol_Button, buttonColor);
        buttonStyle.pushColor(ImGuiCol_ButtonHovered, hoverColor);
        buttonStyle.pushColor(ImGuiCol_ButtonActive, activeColor);
        if (ImGui::Button(kChannelLabels[i], ImVec2(buttonWidth, 0.0f))) {
            channelEnabled[i] = !channelEnabled[i];
            maskChanged       = true;
        }

        if (i + 1 < IM_ARRAYSIZE(kChannelLabels)) {
            ImGui::SameLine();
        }
    }

    return maskChanged;
}

} // namespace ya::ImGuiHelper
