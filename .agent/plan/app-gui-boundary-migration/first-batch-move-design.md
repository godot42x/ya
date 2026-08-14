# Phase A1 — First No-Behavior Move/Rename Batch Design

> 更新时间：2026-08-14
> 状态：Phase A1 设计完成；作为 Batch 1 / Batch 2 真正动代码前的唯一迁移蓝图。

## 1. 设计目标

这一批只解决三件事：

1. 让共享 App 主链从 `Foundation/Core/Application` 物理脱离 `Core`；
2. 让 GUI window/bootstrap/host 主链从 `Framework/AppRuntime` + `Framework/GUI/App` 收口成一条 `GUI/Host`；
3. 让后续真实 patch 有明确的目录、target、include root、兼容层与删除条件。

这一批**不**做：

- Widget/layout/route 行为修改；
- `NativeWindow` 接口拆分实现；
- `Product/Host` 与 `Framework/AppServices` 的 file-level 归位；
- 任何 render / GUI feature 扩张。

## 2. Batch 1 的终局边界

Batch 1 完成后，物理树与 target charter 应收口到下面这条主线：

```text
Core/*
App/Kernel/*
App/Control/*
Render/RHI/*
GUI/Runtime/*
GUI/Host/*
Applications/*
Example/*
```

其中本批只真正动前三段中的 `App/*` 与 `GUI/Host/*`，其它分支只作为依赖背景，不进入迁移 patch。

## 3. 首批 move/rename 范围

### 3.1 App 主链：`Foundation/Core/Application/*`

| 当前文件 | 当前公开 include | 未来目录 | 未来公开 include | 未来 owner / target | compatibility 方案 | 删除条件 |
|---|---|---|---|---|---|---|
| `Foundation/Core/Application/AppKernel.h/.cpp` | `Core/Application/AppKernel.h` | `Engine/Source/App/Kernel/AppKernel.h/.cpp` | `App/Kernel/AppKernel.h` | `ya-app-kernel` | 保留 `Core/Application/AppKernel.h` 转发到新头 | `rg -n 'Core/Application/AppKernel.h' Engine Example Test` 为 0 |
| `Foundation/Core/Application/AutomationRun.h/.cpp` | `Core/Application/AutomationRun.h` | `Engine/Source/App/Control/AutomationRun.h/.cpp` | `App/Control/AutomationRun.h` | `ya-app-control` | 保留旧转发头 | 旧 include spellings 全部迁走 |
| `Foundation/Core/Application/AutomationControlServer.h/.cpp` | `Core/Application/AutomationControlServer.h` | `Engine/Source/App/Control/AutomationControlServer.h/.cpp` | `App/Control/AutomationControlServer.h` | `ya-app-control` | 保留旧转发头 | 同上 |
| `Foundation/Core/Application/GuiEventDriver.h/.cpp` | `Core/Application/GuiEventDriver.h` | `Engine/Source/App/Control/GuiEventDriver.h/.cpp` | `App/Control/GuiEventDriver.h` | `ya-app-control` | 保留旧转发头 | 同上 |
| `Foundation/Core/Application/BmpDiff.h/.cpp` | `Core/Application/BmpDiff.h` | `Engine/Source/App/Control/BmpDiff.h/.cpp` | `App/Control/BmpDiff.h` | `ya-app-control` | 保留旧转发头 | 同上 |

Batch 1 对 `App/*` 的结论：

- `AppKernel` 独立成为 `App/Kernel`；
- 其余控制面、CLI、capture、事件驱动统一进入 `App/Control`；
- `ya-foundation-core` 不再直接 owning 这些源文件，只通过兼容层暂时暴露旧 include 路径。

### 3.2 GUI host 主链：`Framework/AppRuntime/*` + `Framework/GUI/App/*`

| 当前文件 | 当前公开 include | 未来目录 | 未来公开 include | 未来 owner / target | compatibility 方案 | 删除条件 |
|---|---|---|---|---|---|---|
| `Framework/AppRuntime/Bootstrap/AppBootstrap.h/.cpp` | `AppRuntime/AppBootstrap.h` | `Engine/Source/GUI/Host/Bootstrap/AppBootstrap.h/.cpp` | `GUI/Host/AppBootstrap.h` | `ya-gui-host` | 保留 `AppRuntime/AppBootstrap.h` 转发头 | `rg -n 'AppRuntime/AppBootstrap.h' Engine Example Test` 为 0 |
| `Framework/AppRuntime/Window/NativeWindowManager.h/.cpp` | `AppRuntime/NativeWindowManager.h` | `Engine/Source/GUI/Host/Window/NativeWindowManager.h/.cpp` | `GUI/Host/NativeWindowManager.h` | `ya-gui-host` | 保留 `AppRuntime/NativeWindowManager.h` 转发头；`Product/Host/include/Host/NativeWindowManager.h` 暂维持别名 | 所有消费者改用 `GUI/Host/NativeWindowManager.h` |
| `Framework/GUI/App/GUIAppDelegate.h` | `GUI/App/GUIAppDelegate.h` | `Engine/Source/GUI/Host/GUIAppDelegate.h` | `GUI/Host/GUIAppDelegate.h` | `ya-gui-host` | 保留 `GUI/App/GUIAppDelegate.h` 转发头 | `rg -n 'GUI/App/GUIAppDelegate.h' Engine Example Test` 为 0 |
| `Framework/GUI/App/GUIAppHost.h/.cpp` | `GUI/App/GUIAppHost.h` | `Engine/Source/GUI/Host/GUIAppHost.h/.cpp` | `GUI/Host/GUIAppHost.h` | `ya-gui-host` | 保留旧转发头；类名兼容 alias 继续保留 | 旧 include spellings 为 0，且 `GUIAppHost` alias 已无外部用户 |
| `Framework/GUI/App/GUIHeadlessHost.h/.cpp` | `GUI/App/GUIHeadlessHost.h` | `Engine/Source/GUI/Host/GUIHeadlessHost.h/.cpp` | `GUI/Host/GUIHeadlessHost.h` | `ya-gui-host` | 保留旧转发头 | 同上 |
| `Framework/GUI/App/GUIPresentationTarget.h/.cpp` | `GUI/App/GUIPresentationTarget.h` | `Engine/Source/GUI/Host/GUIPresentationTarget.h/.cpp` | `GUI/Host/GUIPresentationTarget.h` | `ya-gui-host` | 保留旧转发头 | 同上 |
| `Framework/GUI/App/include/GUI/App/GUIApp.h` | `GUI/App/GUIApp.h` | `Engine/Source/GUI/Host/include/GUI/Host/GUIApp.h` | `GUI/Host/GUIApp.h` | `ya-gui-host` | 旧头转发到新头 | `rg -n 'GUI/App/GUIApp.h' Engine Example Test` 为 0 |
| `Framework/GUI/App/include/GUI/App/GUIWindowHost.h` | `GUI/App/GUIWindowHost.h` | `Engine/Source/GUI/Host/include/GUI/Host/GUIWindowHost.h` | `GUI/Host/GUIWindowHost.h` | `ya-gui-host` | 旧头转发到新头 | 同上 |

Batch 1 对 `GUI/Host` 的结论：

- `Framework/AppRuntime` 与 `Framework/GUI/App` 不再并列成为两条 “App” 语义；
- `ya-app-runtime` 与 `ya-gui-app-host` 的源文件统一由 `ya-gui-host` owning；
- 旧 target 名仅作为过渡兼容层存在，不再对应真实 owner。

## 4. 不进入 Batch 1 的对象

这些对象虽然和边界有关，但本批只写明延后，不进入真实 move：

1. `Foundation/RHI/NativeWindow.*`
   - 本批只保留 `nativewindow-api-triage.md` 的三分结论；
   - 真正的 `INativeWindow` / `IPresentSurfaceSource` 拆分放到后续独立批次。

2. `Framework/AppServices/*`
   - 先做 file-level consumer audit；
   - 本批不 rename 为新的共享根，也不整体搬到 `Game`。

3. `Product/Host/*`
   - 先审计 shared capability consumer vs app-form shell；
   - 本批只允许修 include 以适配新 public headers，不允许整体迁目录。

## 5. 兼容层策略

### 5.1 兼容层只允许两种形式

1. **forward header**
   - 旧 public include 路径保留一个最小转发头；
   - 头内只 `#include` 新 public 头；
   - 不添加新类型、不塞别名逻辑、不写行为代码。

2. **compat target**
   - 仅当外部 `add_deps()` 改动范围过大时保留一轮；
   - compat target 不再拥有真实源文件，只转发依赖到新 target；
   - compat target 必须在计划中写明删除条件。

### 5.2 明确禁止

- 禁止为了过渡复制两份 `.cpp`；
- 禁止在旧目录继续演化新实现；
- 禁止一边 forward header，一边继续新增旧 include 用法；
- 禁止出现无限期兼容层。

## 6. target rename / 收口顺序

Batch 1 不是简单改目录名；target 也要同时给出过渡顺序。推荐顺序如下：

### Step T1 — 先立真实 owner target，再保留旧名壳

1. 新增 `ya-app-kernel`
   - owning：`Engine/Source/App/Kernel/*`
   - public include root：`Engine/Source/App/Kernel/include`

2. 新增 `ya-app-control`
   - owning：`Engine/Source/App/Control/*`
   - public include root：`Engine/Source/App/Control/include`
   - public deps：`ya-app-kernel`（若 `AutomationRun` / control contracts 需要 `AppKernel` 类型）

3. 新增 `ya-gui-host`
   - owning：`Engine/Source/GUI/Host/*`
   - public include root：`Engine/Source/GUI/Host/include`
   - public deps：`ya-app-kernel`、`ya-app-control`、`ya-rhi`、GUI runtime targets

### Step T2 — 旧 target 变 compat 壳

1. `ya-foundation-core`
   - 停止 owning `Application/*`
   - 如仍需旧 include 兼容，只通过旧转发头提供路径兼容
   - 不应反向依赖 `ya-app-kernel` / `ya-app-control` 去伪装“App 仍属于 Core”

2. `ya-app-runtime`
   - 停止 owning源文件
   - compat 期只 re-export `ya-gui-host`

3. `ya-gui-app-host`
   - 停止 owning源文件
   - compat 期只 re-export `ya-gui-host`

### Step T3 — 消费者切换到真实 target

切换顺序：

1. Example GUI-only consumers：`GUIWorkbench`、`ya-gui-minimal-host`
2. Host-side tests：`ya-gui-headless-host-test`
3. Product-side consumers：`ya-host`、`ya-editor`

理由：先用最干净的 GUI-only 闭包验证迁移方向，再让厚壳接入。

## 7. include root 与 public spellings 迁移规则

### 7.1 新的 public include spellings

- `Core/Application/AppKernel.h` -> `App/Kernel/AppKernel.h`
- `Core/Application/AutomationRun.h` -> `App/Control/AutomationRun.h`
- `Core/Application/AutomationControlServer.h` -> `App/Control/AutomationControlServer.h`
- `Core/Application/GuiEventDriver.h` -> `App/Control/GuiEventDriver.h`
- `Core/Application/BmpDiff.h` -> `App/Control/BmpDiff.h`
- `AppRuntime/AppBootstrap.h` -> `GUI/Host/AppBootstrap.h`
- `AppRuntime/NativeWindowManager.h` -> `GUI/Host/NativeWindowManager.h`
- `GUI/App/GUIApp.h` -> `GUI/Host/GUIApp.h`
- `GUI/App/GUIWindowHost.h` -> `GUI/Host/GUIWindowHost.h`
- `GUI/App/GUIAppHost.h` -> `GUI/Host/GUIAppHost.h`
- `GUI/App/GUIHeadlessHost.h` -> `GUI/Host/GUIHeadlessHost.h`
- `GUI/App/GUIAppDelegate.h` -> `GUI/Host/GUIAppDelegate.h`
- `GUI/App/GUIPresentationTarget.h` -> `GUI/Host/GUIPresentationTarget.h`

### 7.2 修改顺序

1. 先落新 public include root；
2. 再新增旧路径转发头；
3. 再批量改内部 consumers；
4. 最后删旧 spellings。

这样可以避免“移动源文件后，半个仓库暂时没有可用 include path”的中间坏状态。

## 8. build checkpoints

每个步骤必须停下做最小构建断点，不允许把 Batch 1 一次性堆成大 patch。

### Checkpoint C1 — 新 target / 新 include root 能自举

- `xmake b ya-app-kernel`
- `xmake b ya-app-control`
- `xmake b ya-gui-host`

判定：三个新 target 可单独构建；旧 compat target 尚未切 consumer 也不报错。

### Checkpoint C2 — GUI-only 闭包仍干净

- `xmake b ya-gui-headless-host-test`
- `xmake b ya-gui-minimal-host`
- `xmake b GUIWorkbench`

判定：GUI-only 入口仍不被 `Product/Host` / `Scene` / `Game` 语义回灌。

### Checkpoint C3 — Product consumers 接回新主线

- `xmake b ya-host`
- `xmake b ya-editor`

判定：厚壳只是在消费新 public 路径，而不是强行把旧 owner 恢复回来。

### Checkpoint C4 — include spellings 清理前检查

- `rg -n 'Core/Application/' Engine Example Test`
- `rg -n 'AppRuntime/' Engine Example Test`
- `rg -n 'GUI/App/' Engine Example Test`

判定：只允许 compat 头目录自身命中；其余消费者必须已经切到新路径。

## 9. 回退点与停止线

### 9.1 回退点

1. **回退点 R1：新 target 建立后，但旧 consumer 尚未切换前**
   - 如果 include root 或 target 闭包判断错误，直接回退新 target/xmake 变更；
   - 不会影响旧树。

2. **回退点 R2：GUI-only consumers 切换完成后，Product consumers 尚未切换前**
   - 如果 `ya-gui-host` charter 不足以覆盖 GUI-only 主链，可停在这里修正；
   - 不让 `ya-host` / `ya-editor` 的复杂依赖噪声掩盖问题。

3. **回退点 R3：旧 compat 头仍在，但 spellings 已大部分迁走**
   - 若发现 public include 设计有误，还能通过修正转发目标或新 include root 回滚；
   - 还未执行 compatibility 清理，因此回退成本仍低。

### 9.2 停止线

- 如果 `ya-foundation-core` 必须继续拥有 `Application/*` 源文件才能构建，则说明 `App/Kernel` / `App/Control` 闭包设计还不成立，停止；
- 如果 `ya-gui-host` 必须重新依赖 `ya-host` 或任何 Product target 才能构建，则说明 owner 收口方向错误，停止；
- 如果 `GUIWorkbench` 在切换后开始依赖 `Scene` / `Product/Host`，则说明迁移污染 GUI-only 主线，停止；
- 如果为了兼容不得不保留双份实现文件，而不是最小 forward header/compat target，则停止。

## 10. Batch 2 的直接前置物

Batch 1 完成后，下一步不立刻清 compat，而是先补两份审计：

1. `product-host-file-audit.md`
2. `appservices-file-audit.md`

只有这两份 file-level 审计完成后，才能继续拆 `Product/Host` 与 `Framework/AppServices`。

## 11. Batch 1 完成定义

Batch 1 只有在同时满足以下条件时才算完成：

1. `App/Kernel`、`App/Control`、`GUI/Host` 三个真实 owner target 已建立；
2. `Foundation/Core/Application`、`Framework/AppRuntime`、`Framework/GUI/App` 不再 owning真实实现源文件；
3. `GUIWorkbench` 与 `ya-gui-minimal-host` 已通过新 public 路径和新 target 闭包构建；
4. 旧 include 路径只剩 compat 头，不再有普通消费者直接使用；
5. `Product/Host` 与 `Framework/AppServices` 尚未被误搬，而是进入下一批 file-level audit。
