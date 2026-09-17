#pragma once

#include "Core/Api.h"
#include "Render3D/Common/PostProcessingState.h"

namespace ya::postprocess_settings
{

[[nodiscard]] YA_RENDER_3D_API PostProcessingState loadRuntimeSettings(const PostProcessingState& baseline);
YA_RENDER_3D_API void saveRuntimeSettings(const PostProcessingState& settings);

} // namespace ya::postprocess_settings
