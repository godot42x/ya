#pragma once

#include "Core/Base.h"

#include "ECS/System/Render/IMaterialSystem.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/SimpleMaterial.h"


namespace ya
{


struct SimpleMaterialSystem : public IMaterialSystem
{
    struct PushConstant
    {
        glm::mat4 projection = glm::mat4(1.0f);
        glm::mat4 view       = glm::mat4(1.0f);
        glm::mat4 model      = glm::mat4(1.0f);
        uint32_t  colorType  = 0;
    } pc;

    float maxViewDistance = 100.0f;
    // int   culledCount     = 0;
    // int   totalCount      = 0;
    int _defaultColorType = SimpleMaterial::Default;

    // stdptr<IGraphicsPipeline>        _pipeline       = nullptr; // temp move to IMaterialSystem
    stdptr<IPipelineLayout> _pipelineLayout = nullptr;
    SimpleMaterialSystem() : IMaterialSystem("SimpleMaterialSystem") {}

    void onInitImpl(const InitParams& initParams) override;
    void onDestroy() override;
    void onRenderGUI() override;
    void onRender(ICommandBuffer* cmdBuf, const FrameContext* ctx) override;
};
} // namespace ya