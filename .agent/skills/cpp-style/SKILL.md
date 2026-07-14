---
name: ya-cpp-style
description: YA Engine C++ 编码约定、生命周期规则与热路径风格约束。
---

## 适用场景

- 新增或重构 C++ 类 / 方法
- 做代码 review 风格一致性检查
- 生命周期不清晰、所有权混乱、热路径有多余分配

## 命名规范

- 类型：`PascalCase`
- 枚举：`E<Name>::T` 或 `enum class E<Name>`
- 私有成员：`_camelCase`
- 公有成员：`camelCase`
- 局部变量 / 函数：`camelCase`
- 常量：`UPPER_SNAKE_CASE`
- 前缀：接口用 `I`，数据类型可用 `F`

## format
- 优先遵循现有代码风格，保持同一文件内的排版一致。
- 优先纵向排布，同时保持代码紧凑、可读、规律；不要为了压成一行而牺牲可读性，也不要超过常见屏幕宽度。
```cpp
  auto dsls = IDescriptorSetLayout::create(
      _render,
      {
          DescriptorSetLayoutDesc{
              .label    = "Deferred_PBR_MatRes_DSL",
              .set      = 1,
              .bindings = {
                  {.binding = 0, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                  {.binding = 1, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                  {.binding = 2, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                  {.binding = 3, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
                  {.binding = 4, .descriptorType = EPipelineDescriptorType::CombinedImageSampler, .descriptorCount = 1, .stageFlags = EShaderStage::All},
              },
          },
          DescriptorSetLayoutDesc{
              .label    = "Deferred_PBR_Params_DSL",
              .set      = 2,
              .bindings = {{.binding = 0, .descriptorType = EPipelineDescriptorType::UniformBuffer, .descriptorCount = 1, .stageFlags = EShaderStage::Fragment}},
          },
      });
```


## 类布局

1. 成员变量声明放在方法前面。
2. 优先按数据组织类，而不是按 public / private 方法堆叠。
3. 改类定义时先看数据布局是否更清晰，再决定是否要拆方法。
4. 短小、稳定、无额外状态分支的包装函数优先直接写在头文件内联；只有实现较重、依赖较多或需要隐藏实现细节时再放 `.cpp`。

## const 与参数规则

1. 只读局部变量默认加 `const`。
2. 小型基础类型（`bool`、整数、浮点、枚举、裸指针）可值传递。
3. 其余优先 `const T&` 传入。
4. 返回值若引用的是缓存或成员中的稳定对象，优先返回 `const T&`，避免无意义拷贝。

## 热路径规则

1. 每帧路径避免拷贝 `std::string`、`nlohmann::json`、`AssetMeta` 等堆分配对象。
2. map / unordered_map 查询避免重复查找。
3. 后缀判断优先 `ends_with`，不要用 `substr` 造临时字符串。
4. 命中缓存时优先返回引用，不要把缓存值再拷贝一份返回。
5. 热路径上的短包装函数优先头文件内联，减少无意义的跨翻译单元跳转和重复样板实现。

## 抽象与重复代码

1. 先保证代码直白，再考虑抽象。
2. 不要为一次性逻辑或仍在演化的状态流提前造 helper。
3. 只有当重复已经稳定、抽象能明显降低复杂度时，才提取公共逻辑。
4. 尤其是状态机 / resolve 流程，优先保留清晰的 `switch` / 分支结构，而不是勉强统一。

## Switch 规则

- `switch` 中每个 `case` / `default` 的主体都用大括号包住，再写 `break`。
- 即使当前分支只有一两行，也保持同样风格，避免后续扩展时变乱。

## 内存与所有权

1. 生命周期不明确时，不要直接 `new/delete`。
2. 优先使用智能指针；接口返回优先 `shared_ptr`。
3. 项目约定别名：`stdptr<T>` = `std::shared_ptr<T>`。
4. 构造辅助优先使用 `makeShared<T>(...)` / `makeUnique<T>(...)`，不要写错成 `makeshared`。
5. 单例若使用 Meyers' Singleton，`instance()` 实现放 `.cpp`，头文件只保留声明，避免跨模块多实例。
6. `IImageView`、descriptor write、rendering attachment 这类“引用 GPU 对象的轻量包装”默认按 non-owning 看待；除非类型本身明确声明 owning，否则必须由 `Texture`、`RenderImage`、job result、frame context 或 submission context 显式保活。
7. 只要资源句柄被录进 `ICommandBuffer`，就不要假设“函数返回后即可释放”；先确认后端是在 record 时还是 submit/encode 时消费这些对象，再决定保活边界。
8. 对 offscreen job、render graph、异步加载后的提交链路，保活粒度优先挂在 `frame/job/submission result` 上，不要挂在一次 `execute()` 的局部栈对象上。
9. `RenderingInfo::ImageSpec` 这类同时带 `image` 和 `imageView` 的结构，优先从 `imageView` 派生 `image + subresourceRange`，不要在调用点手抄第二份 range 或混搭另一张 image。

## 渲染生命周期红线

1. `beginRendering()`、descriptor 更新、imported image view 创建后，如果对象稍后还会被 command buffer / queue submit 使用，就必须跨过该次 GPU 提交完成。
2. `RenderGraphExecutor`、transient `RenderImage`、per-face/per-mip imported view 这类辅助对象，若其内部持有 attachment/view 资源，不能只活到本地函数返回。
3. `RenderingInfo`、attachment 数组、depth/color attachment 指针进入 record 模式后，不允许再引用栈上临时对象或随后会 reset 的成员缓存。
4. 将 owning 改成 non-owning 时，必须反查所有调用点，把保活责任补到外层；这类改动默认视为高风险，不是“纯重构”。
5. imported graph resource 若复用“已有 owner 的 subresource image view”，必须保证 graph 能看到该 view 的 subresource range；优先让 `IImageView` 自带 range 元数据，必要时再显式补充，不能只传 `shared_ptr<IImageView>` 而让 compiler 退回整张 image 的状态范围。
6. 任何会进入 dynamic rendering / render pass attachment 的 `IImageView`，默认按“可能到 queue submit 才被后端真正消费”处理；不要在同一帧局部作用域里 retire、reset 或覆盖它的 owner。

## 相关 skills

- `render-arch`：改渲染类边界、runtime 编排层时一起看
- `resource-system`：改 resolve 状态机、runtime state、资源生命周期时一起看
- `material-flow`：改材质组件 / runtime material 分层时一起看
- `debug-review`：做提交前风格与风险复盘时一起看
- `code-reorganize`：当风格问题已经演变成文件拆分、目录收敛或职责重组问题时一起看
- `ya-build`：改动引起编译错误或需要验证构建约束时一起看

## 变更约束

1. 只做最小必要改动，不顺手做无关重构。
2. 注释克制，不补显而易见的说明。
3. 生成文件不直接改；若问题来自 shader 头或脚本生成物，回到生成链修。
4. 单个 `.cpp` / `.h` 文件尽量不要超过 1000 行；超过时优先按功能块、helper、pipeline 等稳定职责拆分，而不是继续堆在一个文件里
5. 文件命名按类名或稳定职责收敛；避免 `module.part.h` / `module.part.cpp` 这种分段命名，优先 `AssetTextureManager.h`、`AssetModelManager.cpp`、`AssetTextureImport.cpp` 这类名字
6. 目录结构也按功能与层级收敛；例如 facade/owner、专属导入逻辑、内部 helper 应放在稳定子目录中，而不是长期平铺在同一目录

## 退出条件

- 命名、成员布局、所有权策略符合仓库约定
- 热路径没有明显多余分配或重复查找
- 抽象层级没有因为“顺手整理”而变复杂
