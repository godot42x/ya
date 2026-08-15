# Batch 1 Move Design — Foundation / App / GUI → Framework 容器

> 更新时间：2026-08-15
> 状态：设计完成，作为 Batch 1 动代码前的唯一迁移蓝图。
> 前置：`plan.md`、`../app-gui-boundary-migration/directory-charter.md`（v3 两层）。

## 1. 设计目标与范围

Batch 1 只做「物理分层」：把散落在顶层 / 历史层的 `Foundation`、`App`、`GUI` 全部收进 `Framework` 容器，形成统一两层骨架的第一刀。

**做**：

- `Foundation/Core` → `Framework/Core`
- `Foundation/RHI`（含 Backend） → `Framework/RHI`
- `App/{Kernel,Control,Module}` → `Framework/App/{Kernel,Control,Module}`
- `GUI/Host` → `Framework/GUI/Host`（与 `Framework/GUI/{Runtime,Tooling}` 汇合）

**不做**：

- 任何 target 拆分 / 合并 / 改名（target 名全部不变）
- Scripting / Reflection 独立成 target（仍留在 Core 内）
- `Framework/Game` 的拆分（Batch 2）
- 任何行为 / 接口 / 依赖方向改动

## 2. 核心洞察：include 拼写不变

所有模块的 public include 都已经是「命名空间子目录」形式，移动物理目录后 `#include` 拼写**完全不变**：

| 模块 | include root | 拼写 | 移动后拼写 |
|---|---|---|---|
| `ya-foundation-core` | `Foundation/Core/include/Core/**` | `Core/...` | `Core/...`（不变） |
| `ya-rhi` | `Foundation/RHI/include/RHI/**` | `RHI/...` | 不变 |
| `ya-rhi-backend-common` | `Foundation/RHI/Backend/include/**` | `RHI/Backend/...` | 不变 |
| `ya-rhi-vulkan` | `Foundation/RHI/Backend/Vulkan/include/**` | `RHI/Backend/Vulkan/...` | 不变 |
| `ya-app-kernel` / `-control` / `-module` | `App/*/include/App/**` | `App/Kernel/...` 等 | 不变 |
| `ya-gui-host` | `GUI/Host/include/GUI/**` | `GUI/Host/...` | 不变 |

结论：**C++ 源文件的 `#include` 零改动**。唯一例外是 `Foundation/Core/Reflection/UnifiedReflection.deprecated.h`（内含 `#include "Foundation/Core/Macro/VariadicMacros.h"` 物理路径），经 `rg` 确认已无任何消费者（只有它自己的 include mirror），迁移时直接删除。

## 3. move 表（目录级）

| 当前物理目录 | 目标物理目录 | target（不变） |
|---|---|---|
| `Engine/Source/Foundation/Core/` | `Engine/Source/Framework/Core/` | `ya-foundation-core` |
| `Engine/Source/Foundation/RHI/` | `Engine/Source/Framework/RHI/` | `ya-rhi` + backend |
| `Engine/Source/App/Kernel/` | `Engine/Source/Framework/App/Kernel/` | `ya-app-kernel` |
| `Engine/Source/App/Control/` | `Engine/Source/Framework/App/Control/` | `ya-app-control` |
| `Engine/Source/App/Module/` | `Engine/Source/Framework/App/Module/` | `ya-module-manager` |
| `Engine/Source/GUI/Host/` | `Engine/Source/Framework/GUI/Host/` | `ya-gui-host` |

`Framework/GUI/{Runtime,Tooling}`、`Framework/Hierarchy` 已在 `Framework/` 下，本批不动（Hierarchy 归宿待拍板，见 plan）。

## 4. xmake / 脚本改动清单（精确到文件）

### 4.1 `Engine/Source/xmake.lua`（顶层 includes）

改前 → 改后：

```lua
-- 改前
includes("./App/Kernel/xmake.lua")
includes("./App/Control/xmake.lua")
includes("./App/Module/xmake.lua")
includes("./Foundation/Core/xmake.lua")
includes("./Foundation/RHI/xmake.lua")
includes("./Framework/Hierarchy/xmake.lua")
includes("./Framework/GUI/xmake.lua")
includes("./GUI/Host/xmake.lua")

-- 改后
includes("./Framework/Core/xmake.lua")
includes("./Framework/RHI/xmake.lua")
includes("./Framework/App/Kernel/xmake.lua")
includes("./Framework/App/Control/xmake.lua")
includes("./Framework/App/Module/xmake.lua")
includes("./Framework/Hierarchy/xmake.lua")
includes("./Framework/GUI/xmake.lua")
```

### 4.2 `Engine/Source/Framework/GUI/xmake.lua`

增加 Host 的引入，与 Runtime/Tooling 并列（Host 从顶层移入后由 GUI 聚合统一 include）：

```lua
includes("./Runtime/Resource/xmake.lua")
includes("./Runtime/Draw2D/xmake.lua")
includes("./Runtime/Widgets/xmake.lua")
includes("./Runtime/Compose/xmake.lua")
includes("./Tooling/xmake.lua")
includes("./Host/xmake.lua")   -- 新增
```

`ya-gui-framework` aggregate 的 deps 不变（Host 仍独立，不并入 aggregate；GUI-only 闭包通过 GUIWorkbench 显式链接 `ya-gui-host`）。

### 4.3 `Engine/Source/Framework/RHI/xmake.lua`（shader 生成路径）

移动后 `os.scriptdir()` 由 `Engine/Source/Foundation/RHI` 变 `Engine/Source/Framework/RHI`，相对层数 +1：

```lua
-- 改前
add_includedirs(path.join(os.scriptdir(), "../../../Shader/Slang/Generated/Common"), { public = true })
add_includedirs(path.join(os.scriptdir(), "../../../Shader/GLSL/Generated/Common"), { public = true })
-- 改后
add_includedirs(path.join(os.scriptdir(), "../../../../Shader/Slang/Generated/Common"), { public = true })
add_includedirs(path.join(os.scriptdir(), "../../../../Shader/GLSL/Generated/Common"), { public = true })
```

### 4.4 `Engine/YA.xmake.lua`

- PCH 路径：`set_pcheader("./Source/Foundation/Core/Common/FWD.h")` → `./Source/Framework/Core/Common/FWD.h`
- 注释里的三 product tier 描述同步更新（Foundation/Framework/Product → Framework/Applications 两层）

### 4.5 `Script/ya_module_lint.py`

`MODULES` 字典物理路径同步：

```python
"ya-foundation-core": "Framework/Core",
"ya-rhi": "Framework/RHI",
"ya-rhi-backend-common": "Framework/RHI/Backend",
"ya-rhi-vulkan": "Framework/RHI/Backend/Vulkan",
# App / GUI 相关条目同步为 Framework/App/... / Framework/GUI/...
```

## 5. 本批明确不改

- 所有 target 名（`ya-*`）不变
- 所有导出宏（`YA_CORE_API` / `YA_RHI_API` / `YA_APP_KERNEL_API` / ... / `YA_GUI_API`）不变
- 所有依赖方向不变
- `Foundation/Core/Scripting`、`Foundation/Core/Reflection` 不拆 target，随 Core 整体移动
- `Framework/Game/*` 完全不动（Batch 2）

## 6. build checkpoints

每个 checkpoint 单独构建，不允许一次性堆大 patch。

- **C1 自举**：`xmake b ya-foundation-core` / `ya-rhi` / `ya-app-kernel` / `ya-app-control` / `ya-module-manager` / `ya-gui-host` 全部通过。
- **C2 GUI-only 闭包**：`xmake b GUIWorkbench` / `ya-gui-closure-test` 通过，且不穿进 Scene/Physics/ECS。
- **C3 引擎聚合**：`xmake b ya-engine` 通过（验证 PCH 与聚合 define）。
- **C4 拼写残留**：`rg -n 'Foundation/Core/|Foundation/RHI/|Source/App/|Source/GUI/' Engine Example Test` 仅命中历史注释 / 非活跃文件，活跃消费者零残留。

## 7. 回退点

- **R1**：移动 `Foundation/*` + `App/*` 前，`git mv` 分目录提交，保证任一步可 `git checkout` 回退。
- **R2**：如果 `ya-gui-host` 移入 `Framework/GUI/Host` 后 C2 闭包被破坏，回退 Host 的物理移动，保留顶层 `GUI/Host` 一轮，只先迁 Foundation/App。
- **R3**：如果 RHI 的 shader codegen 路径改错导致 C1 失败，先只修路径（本批最小风险点）。

## 8. 停止线

- 若为迁目录需要新增 compat 转发头或 phony target → 停（本批拼写不变，本不该有）；
- 若某 target 必须保持 `Foundation/` 物理路径才能构建 → 说明 include root 有未识别的绝对路径依赖，停；
- 若 C2 里 GUI-only 闭包开始依赖 Scene/Physics/ECS → 停。

## 9. 完成定义

1. `Framework/{Core,RHI,App,GUI}` 物理目录就位，target 名不变；
2. 顶层 `Foundation/`、`App/`、`GUI/` 目录移除；
3. C1–C4 全绿，`ya-testing` 回归通过；
4. `rg` 确认活跃代码零 `Foundation/`、`Source/App/`、`Source/GUI/` 残留。
