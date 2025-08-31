#pragma once

#include "Core/Base.h"
#include "ECS/Component/BaseMaterialComponent.h"
#include "ECS/System.h"


namespace ya
{

struct BaseMaterialSystem : public IMaterialSystem
{
    alignas(sizeof(glm::vec4)) // push constant alignment requirement
        struct PushConstant
    {
        glm::mat4 viewProjection = glm::mat4(1.0f);
        glm::mat4 model          = glm::mat4(1.0f);
        uint32_t  colorType      = 0;
    } pc;

    float maxViewDistance = 100.0f;
    int   culledCount     = 0;
    int   totalCount      = 0;
    bool  bEntityCamera   = true;

    std::shared_ptr<VulkanPipeline>       _pipeline;
    std::shared_ptr<VulkanPipelineLayout> _pipelineLayout;

    void onInit(VulkanRenderPass *renderPass) override;
    void onDestroy() override;

    void onUpdate(float deltaTime) override;
    void onRender(void *cmdBuf, RenderTarget *rt) override;
    void onRenderGUI() override;
};
} // namespace ya