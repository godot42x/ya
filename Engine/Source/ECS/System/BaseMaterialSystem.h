#pragma once

#include "Core/Base.h"

#include "ECS/System/IMaterialSystem.h"
#include "Platform/Render/Vulkan/VulkanPipeline.h"


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

    std::shared_ptr<VulkanPipeline>       _pipeline;
    std::shared_ptr<VulkanPipelineLayout> _pipelineLayout;

    void onInit(VulkanRenderPass *renderPass) override;
    void onDestroy() override;
    void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }

    void onUpdate(float deltaTime) override;
    void onRender(void *cmdBuf, RenderTarget *rt) override;
};
} // namespace ya