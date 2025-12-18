#pragma once
#include <functional>
#include <glm/glm.hpp>


enum class RenderStage
{
    Setup,       // 初始化渲染通道、清除颜色和深度
    Background,  // 背景渲染
    World3D,     // 3D世界对象渲染
    Transparent, // 半透明物体渲染
    UI2D,        // 2D UI渲染
    Debug,       // 调试元素渲染
    ENUM_AX
};

struct RHICmdList; // Unimplemented

struct CommandBuffer
{};

struct RenderCommand
{
    RenderStage                       stage;
    std::function<void(RHICmdList *)> renderFunc;
    int                               priority = 0;
};



// class RenderPassManager
// {
//     SDL_GPUDevice                          *device = nullptr;
//     std::vector<std::vector<RenderCommand>> stageCommands;

//   public:
//     void init(SDL_GPUDevice *device)
//     {
//         this->device = device;
//         stageCommands.resize(static_cast<size_t>(RenderStage::ENUM_AX));
//     }

//     void cleanup()
//     {
//         // 清除所有渲染命令
//         for (auto &commands : stageCommands) {
//             commands.clear();
//         }
//         stageCommands.clear();
//     }

//     // 添加渲染命令到特定阶段
//     void addPass(RenderStage                       stage,
//                  std::function<void(RHICmdList *)> renderFunc,
//                  int                               priority = 0)
//     {
//         if (stage == RenderStage::ENUM_AX || static_cast<size_t>(stage) >= stageCommands.size()) {
//             return;
//         }

//         RenderCommand command{
//             .stage      = stage,
//             .renderFunc = renderFunc,
//             .priority   = priority};

//         stageCommands[static_cast<size_t>(stage)].push_back(command);
//     }

//     void preExecute()
//     {
//         // 根据优先级排序，优先级高的先渲染
//         for (auto &stages : stageCommands) {
//             std::sort(stages.begin(), stages.end(), [](const RenderCommand &a, const RenderCommand &b) {
//                 return a.priority > b.priority;
//             });
//         }
//     }

//     // 执行所有阶段的渲染
//     void execute(std::shared_ptr<CommandBuffer> cmdBuffer, const glm::vec4 &clearColor)
//     {

//         // // 创建渲染通道参数
//         // SDL_GPURenderPassBeginInfo beginInfo = {};
//         // beginInfo.color_clear_values[0]      = clearColor;
//         // beginInfo.depth_clear_value          = 1.0f; // 深度清除值，1.0f 是最远的深度
//         // beginInfo.stencil_clear_value        = 0;

//         // beginInfo.color_targets[0]  = colorTarget;
//         // beginInfo.num_color_targets = 1;

//         // if (depthTarget) {
//         //     beginInfo.depth_stencil_target     = depthTarget;
//         //     beginInfo.has_depth_stencil_target = true;
//         // }
//         // else {
//         //     beginInfo.has_depth_stencil_target = false;
//         // }

//         // // 开始渲染通道
//         // SDL_GPURenderPass *renderPass = SDL_BeginGPURenderPass(cmdBuffer, &beginInfo);

//         // // 按照阶段顺序执行所有渲染命令
//         // for (size_t stageIndex = 0; stageIndex < stageCommands.size(); ++stageIndex) {
//         //     const auto &commands = stageCommands[stageIndex];
//         //     for (const auto &command : commands) {
//         //         if (command.renderFunc) {
//         //             command.renderFunc(renderPass);
//         //         }
//         //     }
//         // }

//         // // 结束渲染通道
//         // SDL_EndGPURenderPass(renderPass);
//     }
// };
