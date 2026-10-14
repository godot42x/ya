# 技术方案与设计决策

更新：2026-10-14。记录四阶段的具体技术方案、设计权衡和参考实现。

---

## 第一阶段：折叠 Recipe 重复代码

### 问题分析

当前 `DeferredPipelineRecipe::prepareBindings` 和 `appendGraph` 是手写的调用循环，每个功能重复相同模式：

```cpp
// prepareBindings 里重复 8-10 次：
auto ssaoResult = stages.ssao->prepareBindings(resolved.ssao, ctx);
if (ssaoResult.isErr()) {
    YA_CORE_ERROR("Failed to prepare SSAO, error={}", static_cast<int>(ssaoResult.error()));
    return Err(ssaoResult.error());
}
prepared.ssao = ssaoResult.value();

// appendGraph 里重复 8-10 次：
auto ssaoResult = stages.ssao->append(graph, nullptr, graphCtx, facts, resolved.ssao, prepared.ssao, inputs.ssao);
if (ssaoResult.isErr()) {
    YA_CORE_ERROR("Failed to append SSAO pass, error={}", static_cast<int>(ssaoResult.error()));
    return Err(ssaoResult.error());
}
outputs.ssao = ssaoResult.value();
```

**问题：** 新增功能要复制粘贴这段模式 2 次（prepare + append），手动改功能名、类型、错误消息。

### 解决方案

提取模板辅助函数，封装"调用 → 错误处理 → 存储"模式：

```cpp
// FeatureUtils.h（或直接在 FeatureProtocol.h 底部）

template <typename F>
Result<void, FeatureError> prepareFeature(
    const F& feature,
    const typename F::Resolved& resolved,
    const typename F::PrepareContext& ctx,
    typename F::Prepared& outPrepared)
{
    auto result = feature.prepareBindings(resolved, ctx);
    if (result.isErr()) {
        YA_CORE_ERROR("Failed to prepare {}, error={}", 
                      feature.name(), static_cast<int>(result.error()));
        return Err(result.error());
    }
    outPrepared = result.value();
    return Ok();
}

template <typename F>
Result<void, FeatureError> appendFeature(
    const F& feature,
    RenderGraph& graph,
    typename F::Stage* stage,  // 保留以兼容当前 concept
    const FeatureGraphContext& graphCtx,
    const typename F::Facts& facts,
    const typename F::Resolved& resolved,
    const typename F::Prepared& prepared,
    const typename F::Inputs& inputs,
    typename F::GraphOutputs& outOutputs)
{
    auto result = feature.append(graph, stage, graphCtx, facts, resolved, prepared, inputs);
    if (result.isErr()) {
        YA_CORE_ERROR("Failed to append {} pass, error={}", 
                      feature.name(), static_cast<int>(result.error()));
        return Err(result.error());
    }
    outOutputs = result.value();
    return Ok();
}
```

Recipe 里变成：

```cpp
// prepareBindings
if (auto err = prepareFeature(*stages.ssao, resolved.ssao, ctx, prepared.ssao); err.isErr())
    return err;
if (auto err = prepareFeature(*stages.light, resolved.light, ctx, prepared.light); err.isErr())
    return err;
// ... 其余功能各一行

// appendGraph
if (auto err = appendFeature(*stages.ssao, graph, nullptr, graphCtx, facts, 
                             resolved.ssao, prepared.ssao, inputs.ssao, outputs.ssao); err.isErr())
    return err;
// ... 其余功能各一行
```

### 验收标准

- 行数：`prepareBindings` 从 ~200 降到 ~50；`appendGraph` 从 ~350 降到 ~100
- 逻辑等价：单元测试全绿，画面逐像素 0 diff
- 可维护性：新增功能只需补一行调用，不再复制粘贴错误处理

### 风险与缓解

**风险：** 模板实例化可能增加编译时间。  
**缓解：** 功能数量有限（~10 个），模板函数很简单，影响可忽略。如实测编译慢，可改为宏或内联函数。

---

## 第二阶段：统一功能初始化

### 问题分析

Pipeline 的 `init` 里每个功能的签名不同：

```cpp
void DeferredRenderPipeline::init(const InitDesc& desc) {
    _ssaoFeature.init(_render);
    _lightFeature.init(_render, frameAndLightDSL, desc.environmentLightingDSL);
    _gBufferFeature.init(_render, frameAndLightDSL, ...);
    _overlayFeature.init(_render, desc.debugRenderSystem);
    // ... 逐个手写不同调用
}
```

**问题：** 新增功能要在 Pipeline::init 里补特殊调用；Pipeline 必须知道每个功能的初始化依赖。

### 解决方案

#### 方案 A：统一上下文 + 各取所需（推荐）

定义包含所有可能依赖的初始化上下文，每个功能自己决定用哪些字段：

```cpp
struct FeatureInitContext {
    IRender* render;
    stdptr<IDescriptorSetLayout> frameAndLightDSL;
    stdptr<IDescriptorSetLayout> environmentLightingDSL;
    DebugRenderSystem* debugRenderSystem;
    // 未来可扩展其他依赖...
};

// Feature Protocol 要求统一签名：
concept Feature = ... && requires(F& feature, const FeatureInitContext& ctx) {
    { feature.init(ctx) } -> std::same_as<void>;
};

// 每个 Feature 实现：
void SSAOFeature::init(const FeatureInitContext& ctx) {
    // 只用 ctx.render
    _stage = std::make_unique<SSAOStage>(ctx.render);
}

void LightingFeature::init(const FeatureInitContext& ctx) {
    // 用 ctx.render + 两个 DSL
    _stage = std::make_unique<LightStage>(ctx.render);
    _stage->setup(LightStage::SharedInputs{
        .frameAndLightDSL = ctx.frameAndLightDSL,
    });
    _environmentLightingDSL = ctx.environmentLightingDSL;
}
```

Pipeline 的 init 变成表格驱动：

```cpp
void DeferredRenderPipeline::init(const InitDesc& desc) {
    _render = desc.render;
    
    FeatureInitContext ctx{
        .render = desc.render,
        .frameAndLightDSL = frameAndLightDSL,  // Pipeline 构造
        .environmentLightingDSL = desc.environmentLightingDSL,
        .debugRenderSystem = desc.debugRenderSystem,
    };
    
    // 所有功能统一循环
    std::array<IFeature*, 10> features = {
        &_ssaoFeature, &_lightFeature, &_gBufferFeature, 
        &_overlayFeature, &_skyboxFeature, &_bloomFeature,
        &_finalizeFeature, &_spriteFeature, &_entityIdPass, &_shadowFeature
    };
    
    for (auto* feature : features) {
        feature->init(ctx);
    }
}
```

**优点：**
- Pipeline 不再知道每个功能的具体依赖
- 新增功能只需加入 features 数组
- 上下文可扩展，加新依赖不影响不用它的功能

**缺点：**
- 上下文包含所有可能依赖，即使某个功能只用其中一两个
- 需要定义 `IFeature` 基类或用 variant/type-erasure

#### 方案 B：延迟初始化（备选）

不在 Pipeline::init 时初始化功能，而是在首次 prepare 时延迟初始化：

```cpp
// Feature 自己判断是否已初始化
Result<Prepared> SSAOFeature::prepareBindings(const Resolved& resolved, const PrepareContext& ctx) {
    if (!_stage && ctx.render) {
        // 首次调用时初始化
        _stage = std::make_unique<SSAOStage>(ctx.render);
    }
    if (!_stage) {
        return Err(FeatureError::MissingContext);
    }
    // 正常准备...
}
```

**优点：**
- 不需要统一的 init 接口
- 功能完全自治，Pipeline 只负责持有

**缺点：**
- 初始化时机不明确（第一次 prepare 可能是关键路径）
- 错误处理复杂（init 失败与 prepare 失败混在一起）
- 难以保证初始化顺序（某些功能可能依赖其他功能先初始化）

### 推荐方案

**采用方案 A（统一上下文）**，因为：
1. 初始化时机明确（Pipeline 启动时），错误处理清晰
2. 可以保证初始化顺序（数组顺序即初始化顺序）
3. 上下文扩展性好，未来加新依赖不破坏现有功能

### 实施细节

#### IFeature 基类设计

为了让 features 数组能持有不同类型，需要公共基类：

```cpp
struct IFeature {
    virtual ~IFeature() = default;
    virtual void init(const FeatureInitContext& ctx) = 0;
    virtual void shutdown() = 0;
    virtual std::string_view name() const = 0;
};

// 每个具体 Feature 继承：
struct SSAOFeature : public IFeature {
    void init(const FeatureInitContext& ctx) override;
    void shutdown() override;
    std::string_view name() const override { return "SSAO"; }
    
    // Feature Protocol 的其余方法...
    using Settings = ...;
    Resolved resolve(...) const;
    // ...
};
```

**注意：** Feature Protocol 的 concept 检查仍然基于模板，`IFeature` 只用于生命周期管理（init/shutdown）。

#### Shutdown 对称处理

与 init 对应，shutdown 也改为表格驱动：

```cpp
void DeferredRenderPipeline::shutdown() {
    // 逆序销毁（与初始化相反）
    for (auto it = features.rbegin(); it != features.rend(); ++it) {
        (*it)->shutdown();
    }
    
    // 清理 Pipeline 自己的资源
    _frameResources.reset();
    _shadowResources.reset();
    // ...
}
```

### 验收标准

- Pipeline::init 不再有逐个功能的特殊调用，改为统一循环
- 新增功能只需实现 `init(FeatureInitContext)` + 加入数组
- 所有功能初始化成功，设备对象与当前实现等价
- 单元测试全绿

---

## 第三阶段：定义明确的扩展槽位

### 问题分析

当前没有"在哪里可以插入新效果"的明确协议。要加一个新后处理，需要：
1. 实现 Feature
2. 在 Recipe 的某个位置手动插入 prepare 和 append 调用
3. 修改 Pipeline 的 init 添加该功能
4. 修改输入输出结构传递数据

**问题：** 每个新效果都是"自由插入"，没有标准位置和接口约束。

### 解决方案：固定扩展点协议

参考 UE5 SceneViewExtension 和 Unity Custom Pass，定义几个固定扩展点。

#### 扩展点枚举

```cpp
enum class DeferredExtensionPoint {
    AfterGBuffer,        // GBuffer 完成后，lighting 前
                         // 适合：自定义 AO、thickness map、pre-lighting compute
    
    AfterLighting,       // 光照完成后，透明物体前
                         // 适合：SSR、SSGI、自定义光照叠加
    
    BeforePostProcess,   // 场景渲染完成，后处理前
                         // 适合：自定义 resolve、特殊 blit
    
    PostProcessChain,    // 后处理链中（bloom 后、finalize 前）
                         // 适合：自定义色调、滤镜、屏幕效果
};
```

#### 扩展上下文

每个扩展点提供清晰的输入输出：

```cpp
struct DeferredExtensionContext {
    RenderGraph& graph;
    const ViewTargetLease* viewLease;
    const RenderStageContext* stageCtx;
    uint64_t viewId;
    bool bReverseViewportY;
    
    // 根据扩展点不同，可用的资源不同：
    
    // AfterGBuffer 可用：
    std::optional<RGTextureHandle> gBufferRT0;  // Position/Normal
    std::optional<RGTextureHandle> gBufferRT1;  // Albedo
    std::optional<RGTextureHandle> gBufferDepth;
    
    // AfterLighting 可用（包含上面的 + lighting 结果）：
    std::optional<RGTextureHandle> litSceneColor;
    
    // BeforePostProcess 可用（包含所有场景内容）：
    std::optional<RGTextureHandle> finalSceneColor;
    std::optional<RGTextureHandle> sceneDepth;
    
    // PostProcessChain 可用（当前后处理链状态）：
    std::optional<RGTextureHandle> currentColor;  // 链的当前纹理
};

struct DeferredExtensionOutput {
    // 扩展可以：
    // 1. 读取输入，写入新纹理，返回新句柄（链式传递）
    // 2. 直接修改输入纹理（in-place）
    // 3. 什么都不做（pass-through）
    
    std::optional<RGTextureHandle> replacementColor;  // 如果非空，替换链的当前纹理
};
```

#### 扩展 Feature 协议

```cpp
struct IExtensionFeature {
    virtual ~IExtensionFeature() = default;
    
    // 扩展点注册
    virtual DeferredExtensionPoint extensionPoint() const = 0;
    
    // 执行扩展（在扩展点被调用）
    virtual Result<DeferredExtensionOutput, FeatureError> execute(
        DeferredExtensionContext& ctx) = 0;
    
    // 生命周期（与普通 Feature 一样）
    virtual void init(const FeatureInitContext& ctx) = 0;
    virtual void shutdown() = 0;
    virtual std::string_view name() const = 0;
};
```

#### Recipe 集成

Recipe 在每个扩展点位置调用已注册的扩展：

```cpp
// DeferredPipelineRecipe 持有扩展注册表
struct DeferredPipelineRecipe {
    // ...
    
    static void registerExtension(IExtensionFeature* extension) {
        s_extensions.push_back(extension);
    }
    
private:
    static std::vector<IExtensionFeature*> s_extensions;
    
    // 在扩展点调用
    static Result<DeferredExtensionOutput> executeExtensions(
        DeferredExtensionPoint point,
        DeferredExtensionContext& ctx)
    {
        DeferredExtensionOutput output;
        for (auto* ext : s_extensions) {
            if (ext->extensionPoint() == point) {
                auto result = ext->execute(ctx);
                if (result.isErr()) return result;
                
                // 链式传递：如果扩展替换了颜色，更新上下文
                if (result.value().replacementColor.has_value()) {
                    output.replacementColor = result.value().replacementColor;
                    ctx.currentColor = result.value().replacementColor;
                }
            }
        }
        return Ok(output);
    }
};

// appendGraph 里的集成：
Result<GraphOutputs> DeferredPipelineRecipe::appendGraph(...) {
    // ... GBuffer passes ...
    
    // 扩展点 1：AfterGBuffer
    DeferredExtensionContext extCtx{
        .graph = graph,
        .viewLease = viewLease,
        .gBufferRT0 = gbufferOut.rt0,
        .gBufferRT1 = gbufferOut.rt1,
        .gBufferDepth = gbufferOut.depth,
        // ...
    };
    auto afterGBufferResult = executeExtensions(DeferredExtensionPoint::AfterGBuffer, extCtx);
    if (afterGBufferResult.isErr()) return Err(afterGBufferResult.error());
    
    // ... Lighting passes ...
    
    // 扩展点 2：AfterLighting
    extCtx.litSceneColor = lightingOut.sceneColor;
    auto afterLightingResult = executeExtensions(DeferredExtensionPoint::AfterLighting, extCtx);
    if (afterLightingResult.isErr()) return Err(afterLightingResult.error());
    
    // ... 其余扩展点 ...
}
```

#### 示例：自定义 SSGI 扩展

```cpp
class SSGIExtension : public IExtensionFeature {
public:
    DeferredExtensionPoint extensionPoint() const override {
        return DeferredExtensionPoint::AfterLighting;
    }
    
    Result<DeferredExtensionOutput, FeatureError> execute(
        DeferredExtensionContext& ctx) override
    {
        if (!ctx.litSceneColor.has_value() || !ctx.gBufferDepth.has_value()) {
            return Err(FeatureError::MissingContext);
        }
        
        // 添加 SSGI pass
        auto ssgiOutput = ctx.graph.addPass("SSGI", [&](RenderGraph::Builder& builder) {
            auto sceneColor = builder.read(*ctx.litSceneColor);
            auto depth = builder.read(*ctx.gBufferDepth);
            auto output = builder.createTexture("SSGIResult", /*...*/);
            
            builder.execute([=](RenderGraphExecutor& exec) {
                // SSGI 计算...
            });
            
            return output;
        });
        
        // 替换场景颜色（后续 pass 使用 SSGI 结果）
        return Ok(DeferredExtensionOutput{
            .replacementColor = ssgiOutput
        });
    }
    
    void init(const FeatureInitContext& ctx) override {
        // 初始化 SSGI 相关资源...
    }
    
    void shutdown() override { /* ... */ }
    std::string_view name() const override { return "SSGI"; }
};

// 注册（在 Pipeline 或某个初始化入口）：
static SSGIExtension s_ssgiExtension;
DeferredPipelineRecipe::registerExtension(&s_ssgiExtension);
```

### 验收标准

- 扩展点协议清晰：每个点的输入输出文档化
- 真实功能验证：接入一个示例扩展（GTAO 或 SSR），只需实现 + 注册
- 修改范围：记录改了几个文件、多少行，主要限于扩展实现本身
- Recipe 内部不变：扩展点调用是固定代码，不因具体扩展而修改

### 风险与权衡

**风险：** 固定扩展点可能不覆盖所有需求，某些特殊效果想插在"非标准位置"。  
**缓解：** 
1. 扩展点可以根据实际需求增加（不是一次性定死）
2. 如果确实需要非标准位置，仍可修改 Recipe（扩展点是便利，不是唯一方式）
3. 覆盖 80% 的常见需求即可，不追求 100%

---

## 第四阶段：完成现有功能封装

这一阶段是清理遗留代码，没有新设计，主要是执行前三阶段确立的模式。

### SSAO 封装具体步骤

见 `migration.md` 的详细文件清单。核心是：

1. 删除 Recipe 里的临时 `const SSAOFeature ssaoFeature;`
2. Recipe 改为调用 Pipeline 的 `_ssaoFeature`（传指针或引用）
3. 删除 `SSAOFeature::stage()` 和 `PrepareContext::stage`
4. `_stage` / `_settings` 改为 private
5. 测试边界情况（未初始化、关闭、参数冻结）

### 其他功能

按需调整，不强求立刻全部封装。优先级：

1. **高优先级（简单且收益大）**：Bloom、Finalize、Skybox、Overlay（已持有设备，只需删除外部访问器）
2. **中优先级（需协调）**：Lighting（依赖外部 DSL，需确认接口）、GBuffer（同 Lighting）
3. **低优先级（复杂或依赖外部）**：Shadow（等灯光线收敛）、ForwardViewStage（共享容器需拆分设计）、Sprite2D/EntityId（非核心管线）

---

## 设计原则与权衡

### 原则 1：渐进式改善，不推翻重写

每个阶段独立验收、独立交付价值，不等全部完成。即使只完成第一阶段，也能看到 Recipe 代码量减半。

### 原则 2：固定核心，灵活外围

GBuffer/lighting/shadow 的顺序和结构保持稳定，后处理和视觉效果层提供扩展能力。不追求"任意重组管线"。

### 原则 3：数据驱动的边界

设置和参数可配置（如 PostProcessingState），执行逻辑保持代码（Recipe 不变成 JSON）。不为了"数据驱动"而数据驱动。

### 原则 4：封装但不过度抽象

Feature 封装设备实现，但不要求"零依赖"。Lighting 依赖外部 DSL 是合理的（那是共享资源），不强求每个 Feature 完全自治。

### 原则 5：优化可读性和维护性，不优化编译时间或运行性能

模板辅助函数、虚函数调用可能略增编译时间和运行开销，但换来的是更清楚的代码和更低的维护成本。性能优化不是本次目标（但不应有明显退化）。

---

## 参考实现与灵感

- **UE5 SceneViewExtension**：`Engine/Source/Runtime/Engine/Public/SceneViewExtension.h`
- **Unity HDRP Custom Pass**：`com.unity.render-pipelines.high-definition/Runtime/RenderPipeline/CustomPass/`
- **Godot 4 Compositor**：`servers/rendering/renderer_rd/effects/` 和 `RenderingEffect` 类
- **Frostbite RenderGraph**：GDC 演讲"FrameGraph: Extensible Rendering Architecture in Frostbite"

关键借鉴：
- **扩展点 > 完全开放**：固定槽位降低复杂度
- **上下文携带输入输出**：扩展不需要全局查询
- **表格驱动 > 手写循环**：统一机制降低重复
