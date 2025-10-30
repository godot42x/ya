#pragma once

#include "Core/Base.h"

#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/Pipeline.h"


namespace ya
{


struct BaseMaterialSystem : public IMaterialSystem
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

    std::shared_ptr<IPipelineLayout> _pipelineLayoutOwner;
    stdptr<IGraphicsPipeline>        _pipeline       = nullptr;
    stdptr<IPipelineLayout>          _pipelineLayout = nullptr;

    void onInit(IRenderPass *renderPass) override;
    void onDestroy() override;
    void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }

    void onUpdate(float deltaTime) override;
    void onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt) override;
};
} // namespace ya