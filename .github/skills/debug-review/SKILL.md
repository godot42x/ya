---
name: ya-debug-review
description: YA Engine 崩溃排查、变更自检与提交前 review 清单。
---

## 适用场景

- 运行时崩溃、访问越界、空指针、设备丢失
- 提交前自检、代码 review、diff 复盘
- 需要判断某次改动是否越界、是否引入了时序 / 生命周期风险

## 崩溃排查优先级

1. `0xC0000005`：先查空指针、悬空指针、失效引用。
2. 初始化顺序：确认 `RenderRuntime`、`IRender`、system、component resolve 顺序正确。
3. 资源生命周期：检查 `shared_ptr` / runtime state 持有链与释放时机。
4. Vulkan 问题：优先检查 frame 内资源重建、layout transition、descriptor / imageView 是否失效。

## 日志与观测

优先使用：
- `YA_CORE_TRACE`
- `YA_CORE_DEBUG`
- `YA_CORE_INFO`
- `YA_CORE_WARN`
- `YA_CORE_ERROR`
- `YA_CORE_ASSERT`

不要用 `std::cout` / `printf` 临时打日志。

## 相关 skills

- `build`：先确认构建、测试、shader 生成入口是否正确
- `vscode`：问题只在 VS Code 调试链路复现时一起看
- `render-arch`：Vulkan / RenderRuntime / layout 问题时一起看
- `resource-system`：资源生命周期、descriptor、resolve 问题时一起看
- `material-flow`：材质串扰、authoring/runtime 不一致时一起看
- `cpp-style`：review 时收敛所有权、最小改动和抽象层级时一起看

## Review 检查单

1. `git diff` 只包含当前任务需要的最小改动。
2. 不直接编辑生成文件；生成结果有问题时回到脚本或 xmake 规则修。
3. 不引入平行抽象或无关重构。
4. 对状态流、resolve 流程、渲染编排，优先保持清晰分支，不做提前抽象。
5. 临时调试代码、一次性文件、无效分支在提交前清理掉。
6. 若改到 Vulkan 资源，确认没有在 frame recording 中途重建正在使用的对象。

## 常见高风险点

- component 与 runtime cache 双写，导致单一事实源失效
- descriptor / imageView 更新了部分 consumer，另一条管线仍拿旧资源
- 场景拓扑创建与普通 resolve 逻辑混在一起
- editor pretty name / raw field path 混用，导致属性修改链路断裂

## 退出条件

- 崩溃或异常路径已定位到明确模块
- 改动边界清晰，可解释、可回归
- review 后没有遗留生成文件误改、临时代码或明显越界修改