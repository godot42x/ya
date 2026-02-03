#pragma once
#include "Core/Base.h"

#include "ECS/Component.h"
#include "ECS/Component/TransformComponent.h"
#include "ECS/System/IMaterialSystem.h"
#include "Render/Core/Buffer.h"
#include "Render/Core/DescriptorSet.h"
#include "Render/Core/IRenderTarget.h"
#include "Render/Core/Pipeline.h"
#include "Render/Material/MaterialFactory.h"
#include "Render/RenderDefines.h"
#include "Scene/Scene.h"
#include <format>
#include <functional>

namespace ya
{

/**
 * @brief 材质系统基类模板
 *
 * 提供材质系统的通用功能框架，包括：
 * - Pipeline 和 PipelineLayout 管理
 * - Descriptor Set 布局和池管理
 * - Frame UBO 管理
 * - Material 参数和资源的 Descriptor Set 管理
 * - 通用渲染循环逻辑
 *
 * @tparam TMaterial 材质类型
 * @tparam TMaterialComponent 材质组件类型
 * @tparam TFrameUBO Frame UBO 结构体类型
 * @tparam TMaterialParamUBO Material 参数 UBO 结构体类型
 */
template <typename TMaterial, typename TMaterialComponent, typename TFrameUBO, typename TMaterialParamUBO>
struct MaterialSystemV1Template : public IMaterialSystem
{
    // ========================================================================
    // 常量定义
    // ========================================================================
    static constexpr uint32_t NUM_MATERIAL_BATCH     = 16;
    static constexpr uint32_t NUM_MATERIAL_BATCH_MAX = 2048;

    // ========================================================================
    // 类型定义
    // ========================================================================
    using MaterialType          = TMaterial;
    using MaterialComponentType = TMaterialComponent;
    using FrameUBOType          = TFrameUBO;
    using MaterialParamType     = TMaterialParamUBO;

    // ========================================================================
    // Pipeline 相关资源
    // ========================================================================
    GraphicsPipelineCreateInfo         _pipelineDesc;
    std::shared_ptr<IPipelineLayout>   _pipelineLayout;
    std::shared_ptr<IGraphicsPipeline> _pipeline;

    // ========================================================================
    // Descriptor Set Layout
    // ========================================================================
    std::shared_ptr<IDescriptorSetLayout> _materialFrameDSL;    // set 0: per-frame (camera, time, etc.)
    std::shared_ptr<IDescriptorSetLayout> _materialParamDSL;    // set 1: per-material params
    std::shared_ptr<IDescriptorSetLayout> _materialResourceDSL; // set 2: per-material resources (textures)

    // ========================================================================
    // Frame Descriptor Set (set 0)
    // ========================================================================
    stdptr<IDescriptorPool> _frameDSP; // Frame descriptor pool
    DescriptorSetHandle     _frameDS;  // Frame descriptor set
    stdptr<IBuffer>         _frameUBO; // Frame uniform buffer

    // ========================================================================
    // Material Descriptor Sets (动态扩展)
    // ========================================================================
    uint32_t                         _lastMaterialDSCount = 0;
    std::shared_ptr<IDescriptorPool> _materialDSP; // Material descriptor pool

    std::vector<std::shared_ptr<IBuffer>> _materialParamsUBOs;  // Material parameter UBOs
    std::vector<DescriptorSetHandle>      _materialParamDSs;    // Material param descriptor sets
    std::vector<DescriptorSetHandle>      _materialResourceDSs; // Material resource descriptor sets

    // ========================================================================
    // 调试信息
    // ========================================================================
    std::string _ctxEntityDebugStr;

    // ========================================================================
    // 生命周期方法（需要子类实现）
    // ========================================================================
    virtual void onInit(IRenderPass *renderPass) override                     = 0;
    virtual void onDestroy() override                                         = 0;
    virtual void onUpdate(float deltaTime) override                           = 0;
    virtual void onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt) override = 0;

    virtual void onRenderGUI() override
    {
        IMaterialSystem::onRenderGUI();
    }

  protected:
    // ========================================================================
    // 辅助方法（供子类调用）
    // ========================================================================

    /**
     * @brief 创建 Descriptor Set Layouts
     * @param render 渲染器
     * @param descriptorSetLayouts Descriptor Set Layout 描述
     * @return 创建的 Descriptor Set Layouts
     */
    std::vector<std::shared_ptr<IDescriptorSetLayout>> createDescriptorSetLayouts(
        IRender                                *render,
        const std::vector<DescriptorSetLayout> &descriptorSetLayouts)
    {
        return IDescriptorSetLayout::create(render, descriptorSetLayouts);
    }

    /**
     * @brief 创建 Pipeline Layout
     * @param render 渲染器
     * @param label Pipeline Layout 名称
     * @param pushConstants Push Constant 范围
     * @param DSLs Descriptor Set Layouts
     * @return 创建的 Pipeline Layout
     */
    std::shared_ptr<IPipelineLayout> createPipelineLayout(IRender                                                  *render,
                                                          const std::string                                        &label,
                                                          const std::vector<PushConstantRange>                     &pushConstants,
                                                          const std::vector<std::shared_ptr<IDescriptorSetLayout>> &DSLs)
    {
        return IPipelineLayout::create(render, label, pushConstants, DSLs);
    }

    /**
     * @brief 创建 Graphics Pipeline
     * @param render 渲染器
     * @param renderPass Render Pass
     * @param pipelineLayout Pipeline Layout
     * @return 创建的 Graphics Pipeline
     */
    std::shared_ptr<IGraphicsPipeline> createGraphicsPipeline(
        IRender         *render,
        IRenderPass     *renderPass,
        IPipelineLayout *pipelineLayout)
    {
        return IGraphicsPipeline::create(render, renderPass, pipelineLayout);
    }

    // ========================================================================
    // 通用实现的方法
    // ========================================================================

    /**
     * @brief 设置视口和裁剪区域
     * @param cmdBuf 命令缓冲区
     * @param rt 渲染目标
     */
    void setupViewportAndScissor(ICommandBuffer *cmdBuf, IRenderTarget *rt)
    {
        uint32_t width  = rt->getFrameBuffer()->getWidth();
        uint32_t height = rt->getFrameBuffer()->getHeight();

        float viewportY      = 0.0f;
        float viewportHeight = static_cast<float>(height);
        if (bReverseViewportY) {
            viewportY      = static_cast<float>(height);
            viewportHeight = -static_cast<float>(height);
        }

        cmdBuf->setViewport(0.0f, viewportY, static_cast<float>(width), viewportHeight, 0.0f, 1.0f);
        cmdBuf->setScissor(0, 0, width, height);
        cmdBuf->setCullMode(_cullMode);
    }

    /**
     * @brief 重建材质 Descriptor Pool（通用实现）
     * @param materialCount 材质数量
     * @param poolSizeCalculator 计算 Pool Size 的回调函数
     *
     * 通用的 Descriptor Pool 重建逻辑，支持通过回调自定义 pool sizes
     */
    void recreateMaterialDescPoolImpl(
        uint32_t                                                 materialCount,
        std::function<std::vector<DescriptorPoolSize>(uint32_t)> poolSizeCalculator)
    {
        auto *render = getRender();
        YA_CORE_ASSERT(render != nullptr, "Render is null");

        // 1. 计算所需的 descriptor set 数量
        uint32_t newDescriptorSetCount = std::max<uint32_t>(1, _lastMaterialDSCount);
        if (_lastMaterialDSCount == 0) {
            _lastMaterialDSCount = newDescriptorSetCount;
        }

        while (newDescriptorSetCount < materialCount) {
            newDescriptorSetCount *= 2;
        }
        if (newDescriptorSetCount > NUM_MATERIAL_BATCH_MAX) {
            YA_CORE_ASSERT(false, "Too many materials, exceed the max limit");
            return;
        }

        // 2. 清空旧的 descriptor sets
        _materialParamDSs.clear();
        _materialResourceDSs.clear();

        // 3. 重置或重建 pool
        if (_materialDSP) {
            _materialDSP->resetPool();
        }

        DescriptorPoolCreateInfo poolCI{
            .maxSets   = newDescriptorSetCount * 2, // param + resource
            .poolSizes = poolSizeCalculator(newDescriptorSetCount),
        };
        _materialDSP = IDescriptorPool::create(render, poolCI);

        // 4. 分配新的 descriptor sets
        _materialDSP->allocateDescriptorSets(_materialParamDSL, newDescriptorSetCount, _materialParamDSs);
        _materialDSP->allocateDescriptorSets(_materialResourceDSL, newDescriptorSetCount, _materialResourceDSs);

        for (auto &ds : _materialParamDSs) {
            YA_CORE_ASSERT(ds.ptr != nullptr, "Failed to allocate material param descriptor set");
        }

        // 5. 创建额外的 UBOs
        uint32_t diffCount = newDescriptorSetCount - _materialParamsUBOs.size();
        for (uint32_t i = 0; i < diffCount; i++) {
            auto buffer = ya::IBuffer::create(
                render,
                ya::BufferCreateInfo{
                    .label         = std::format("{}_Param_UBO", _label),
                    .usage         = ya::EBufferUsage::UniformBuffer,
                    .size          = sizeof(MaterialParamType),
                    .memProperties = ya::EMemoryProperty::HostVisible | ya::EMemoryProperty::HostCoherent,
                });
            _materialParamsUBOs.push_back(buffer);
        }

        _lastMaterialDSCount = newDescriptorSetCount;
    }

    /**
     * @brief 通用渲染循环
     * @param cmdBuf 命令缓冲区
     * @param rt 渲染目标
     * @param pushConstantUpdater 更新 Push Constant 的回调
     *
     * 处理材质系统的通用渲染流程，包括：
     * - 获取场景和视图
     * - 绑定 pipeline
     * - 设置视口和裁剪区域
     * - 更新 descriptor sets
     * - 绑定 descriptor sets 和绘制
     */
    template <typename TPushConstant>
    void renderImpl(
        ICommandBuffer                                                                 *cmdBuf,
        IRenderTarget                                                                  *rt,
        std::function<TPushConstant(const TransformComponent &tc)>                      pushConstantUpdater,
        std::function<void(const MaterialComponentType &, const TransformComponent &)> preDrawCallback = nullptr)
    {
        Scene *scene = getActiveScene();
        if (!scene) {
            return;
        }

        auto view = scene->getRegistry().view<MaterialComponentType, TransformComponent>();
        if (view.begin() == view.end()) {
            return;
        }

        // 绑定 pipeline
        cmdBuf->bindPipeline(_pipeline.get());

        // 设置视口和裁剪区域
        setupViewportAndScissor(cmdBuf, rt);

        // 更新 frame descriptor set
        updateFrameDS(rt);

        // 检查是否需要扩展材质 descriptor pool
        bool     bShouldForceUpdateMaterial = false;
        uint32_t materialCount              = MaterialFactory::get()->getMaterialSize<MaterialType>();
        if (materialCount > _lastMaterialDSCount) {
            recreateMaterialDescPool(materialCount);
            bShouldForceUpdateMaterial = true;
        }

        std::vector<bool> updatedMaterial(materialCount, false);

        // 遍历所有实体
        for (entt::entity entity : view) {
            const auto &[name, materialComp, tc] = view.get(entity);

            // 预绘制回调
            if (preDrawCallback) {
                preDrawCallback(name, materialComp, tc);
            }

            // 遍历材质和 mesh
            for (const auto &[material, meshIDs] : materialComp.getMaterial2MeshIDs()) {
                _ctxEntityDebugStr = std::format("{} (Mat: {})", name.getName(), material->getLabel());

                if (!material || material->getIndex() < 0) {
                    YA_CORE_WARN("default material for none or error material");
                    continue;
                }

                // 获取材质索引和 descriptor sets
                uint32_t            materialInstanceIndex = material->getIndex();
                DescriptorSetHandle paramDS               = _materialParamDSs[materialInstanceIndex];
                DescriptorSetHandle resourceDS            = _materialResourceDSs[materialInstanceIndex];

                // 更新 descriptor sets（避免重复更新）
                if (!updatedMaterial[materialInstanceIndex]) {
                    if (bShouldForceUpdateMaterial || material->isParamDirty()) {
                        updateMaterialParamDS(paramDS, material);
                        material->setParamDirty(false);
                    }
                    if (bShouldForceUpdateMaterial || material->isResourceDirty()) {
                        updateMaterialResourceDS(resourceDS, material);
                        material->setResourceDirty(false);
                    }
                    updatedMaterial[materialInstanceIndex] = true;
                }

                // 绑定 descriptor sets
                std::vector<DescriptorSetHandle> descSets = {
                    _frameDS,
                    resourceDS,
                    paramDS,
                };
                cmdBuf->bindDescriptorSets(_pipelineLayout.get(), 0, descSets);

                // 更新 push constants
                TPushConstant pushConst = pushConstantUpdater(tc);
                cmdBuf->pushConstants(_pipelineLayout.get(),
                                      EShaderStage::Vertex,
                                      0,
                                      sizeof(TPushConstant),
                                      &pushConst);

                // 绘制每个 mesh
                for (const auto &meshIndex : meshIDs) {
                    auto mesh = materialComp.getMesh(meshIndex);
                    if (mesh) {
                        mesh->draw(cmdBuf);
                    }
                }
            }
        }
    }

    // ========================================================================
    // 需要子类实现的方法
    // ========================================================================

    /**
     * @brief 重建材质 Descriptor Pool
     * @param materialCount 材质数量
     *
     * 子类可以直接调用 recreateMaterialDescPoolImpl 并提供 poolSizeCalculator Descriptor Pool
     * 需要子类实现具体逻辑
     */
    virtual void recreateMaterialDescPool(uint32_t materialCount) = 0;

    /**
     * @brief 更新 Frame Descriptor Set
     * @param rt Render Target
     *
     * 更新 per-frame 数据（相机矩阵、时间等）
     * 需要子类实现具体逻辑
     */
    virtual void updateFrameDS(IRenderTarget *rt) = 0;

    /**
     * @brief 更新 Material 参数 Descriptor Set
     * @param ds Descriptor Set
     * @param material 材质
     *
     * 更新材质参数（颜色、roughness 等）
     * 需要子类实现具体逻辑
     */
    virtual void updateMaterialParamDS(DescriptorSetHandle ds, TMaterial *material) = 0;

    /**
     * @brief 更新 Material 资源 Descriptor Set
     * @param ds Descriptor Set
     * @param material 材质
     *
     * 更新材质资源（纹理、采样器等）
     * 需要子类实现具体逻辑
     */
    virtual void updateMaterialResourceDS(DescriptorSetHandle ds, TMaterial *material) = 0;
};

// ========================================================================
// 使用示例（注释）
// ========================================================================
/*
// 1. 定义材质参数 UBO
struct MyMaterialParamUBO {
    glm::vec4 baseColor;
    float roughness;
    float metallic;
};

// 2. 定义 Frame UBO
struct MyFrameUBO {
    glm::mat4 projection;
    glm::mat4 view;
    glm::vec2 resolution;
    float time;
};

// 3. 定义 Push Constant
struct MyPushConstant {
    glm::mat4 modelMatrix;
};

// 4. 定义自定义材质系统
struct MyMaterialSystem : public MaterialSystemBase<MyMaterial, MyMaterialComponent, MyFrameUBO, MyMaterialParamUBO>
{
    void onInit(IRenderPass *renderPass) override
    {
        _label = "MyMaterialSystem";
        IRender *render = getRender();

        // 创建 Descriptor Set Layouts
        PipelineLayoutDesc pipelineLayout{
            .label = "MyMaterialSystem_PipelineLayout",
            .pushConstants = {
                PushConstantRange{
                    .offset = 0,
                    .size = sizeof(MyPushConstant),
                    .stageFlags = EShaderStage::Vertex,
                },
            },
            .descriptorSetLayouts = {
                // ... descriptor set layout 定义
            },
        };

        auto DSLs = createDescriptorSetLayouts(render, pipelineLayout.descriptorSetLayouts);
        _materialFrameDSL = DSLs[0];
        _materialParamDSL = DSLs[1];
        _materialResourceDSL = DSLs[2];

        _pipelineLayout = createPipelineLayout(render, pipelineLayout.label, pipelineLayout.pushConstants, DSLs);

        // 创建 Graphics Pipeline
        _pipeline = createGraphicsPipeline(render, renderPass, _pipelineLayout.get());
        _pipeline->recreate(_pipelineDesc);

        // 初始化 Frame Descriptor Pool 和 UBO
        // ...

        // 初始化材质 Descriptor Pool
        recreateMaterialDescPool(NUM_MATERIAL_BATCH);
    }

    void onDestroy() override
    {
        // 清理资源
    }

    void onUpdate(float deltaTime) override
    {
        // 更新逻辑
    }

    void onRender(ICommandBuffer *cmdBuf, IRenderTarget *rt) override
    {
        // 使用通用渲染实现
        renderImpl<MyPushConstant>(
            cmdBuf,
            rt,
            [](const TransformComponent &tc) -> MyPushConstant {
                return MyPushConstant{
                    .modelMatrix = tc.getTransform(),
                };
            });
    }

protected:
    void recreateMaterialDescPool(uint32_t materialCount) override
    {
        // 使用通用实现，提供 pool size 计算函数
        recreateMaterialDescPoolImpl(materialCount, [](uint32_t count) {
            return std::vector<DescriptorPoolSize>{
                DescriptorPoolSize{
                    .type = EPipelineDescriptorType::UniformBuffer,
                    .descriptorCount = count,
                },
                DescriptorPoolSize{
                    .type = EPipelineDescriptorType::CombinedImageSampler,
                    .descriptorCount = count * 2, // 假设每个材质有 2 个纹理
                },
            };
        });
    }

    void updateFrameDS(IRenderTarget *rt) override
    {
        // 更新 Frame UBO
        MyFrameUBO frameData;
        // ... 填充 frameData
        _frameUBO->update(&frameData, sizeof(MyFrameUBO), 0);

        // 更新 Descriptor Set
        // ...
    }

    void updateMaterialParamDS(DescriptorSetHandle ds, MyMaterial *material) override
    {
        // 更新材质参数
        auto ubo = _materialParamsUBOs[material->getIndex()];
        MyMaterialParamUBO paramData = material->getParamUBO();
        ubo->update(&paramData, sizeof(MyMaterialParamUBO), 0);

        // 绑定到 Descriptor Set
        // ...
    }

    void updateMaterialResourceDS(DescriptorSetHandle ds, MyMaterial *material) override
    {
        // 更新材质资源（纹理等）
        // ...
    }
};
*/

} // namespace ya
