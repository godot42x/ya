#include "DebugSkinning.h"

#include "ImGuiHelper.h"

namespace ya
{

void DebugSkinning::renderGUI()
{
    if (ImGui::TreeNode("Debug Skinning")) {
        ImGui::Checkbox("Debug Skinning", &bEnabled);
        ImGui::InputInt("Debug Skinning Bone", &pickingBone, 1, 0);
        if (_pipeline) {
            _pipeline->renderGUI();
        }
        ImGui::TreePop();
    }
}

} // namespace ya
