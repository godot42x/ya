#pragma once

#include "Runtime/GUI/ImGui/ImGuiSystem.h"

#include <array>

namespace ya::ImGuiHelper
{

ComponentMapping BuildRGBAChannelMaskMapping(const std::array<bool, 4>& channelEnabled);
bool             IsIdentityRGBAChannelMask(const std::array<bool, 4>& channelEnabled);
bool             RenderRGBAChannelMaskButtons(std::array<bool, 4>& channelEnabled, float frameRounding = 6.0f);

} // namespace ya::ImGuiHelper
