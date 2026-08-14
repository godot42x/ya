# App / GUI 边界迁移进度记录

> 建立日期：2026-08-14
> 作用：记录这条新迁移主线从旧 GUI convergence 计划中拆出后的推进情况。

## 2026-08-14 — 从 `gui-architecture-convergence` 拆出独立迁移计划

### 本轮完成

- 新建 `app-gui-boundary-migration/`，把还未完成的目录 / target / owner 迁移主线单独立项；
- 将活跃输入工件迁入本计划目录：`owner-model.md`、`directory-charter.md`、`capability-appform-mapping.md`、`nativewindow-api-triage.md`、`directory-target-include-audit.md`；
- 旧的 `gui-architecture-convergence` 计划改为历史基线，不再作为活跃迁移待办的默认入口；
- 新计划已补齐最低执行工件：`plan.md`、`todo.md`、`progress.md`、`feature_matrix.json`、`session_checklist.md`。

### 当前结论

- 现在的活跃问题已经不再是 GUI 内核本身，而是如何把已经定下来的 owner / 目录 / target 语义做成真实的 no-behavior 迁移批次；
- 最关键的剩余模糊区仍然是 `Product/Host` 与 `Framework/AppServices`；这两块必须先做 file-level audit，再动目录。

### 下一轮直接接力点

1. 写第一轮 move/rename batch 设计；
2. 做 `Product/Host` / `Framework/AppServices` file-level consumer audit；
3. 再开 Batch 1 迁移 patch。

### 本轮验证

- 文档级验证：新目录最低工件已补齐；
- 文档级验证：活跃迁移输入工件已迁到新目录；
- 代码级验证：本轮未改 `Engine/Source` 行为代码。

## 2026-08-14 — 完成 Batch 1 no-behavior 迁移设计

### 本轮完成

- 新增 `first-batch-move-design.md`，把 Batch 1 的 move/rename 范围落到文件级；
- 明确了 `Foundation/Core/Application/*` -> `App/Kernel` + `App/Control` 的迁移表；
- 明确了 `Framework/AppRuntime/*` + `Framework/GUI/App/*` -> `GUI/Host/*` 的迁移表；
- 定义了 forward-header / compat-target 的唯一过渡形式、删除条件、build checkpoints 与回退点；
- 把当前活跃切片从 Phase A1 设计切换到 Phase A2 file-level consumer audit。

### 当前结论

- Batch 1 现在已经不是抽象口号，而是一份可直接转为 patch 的迁移蓝图；
- 下一步的真实风险不在 `App/*` 和 `GUI/Host/*`，而在 `Product/Host` 与 `Framework/AppServices` 这两个混合桶；
- 因此继续动代码前，先补 `Product/Host` 与 `Framework/AppServices` 的 file-level audit 是必要前置。

### 下一轮直接接力点

1. 输出 `product-host-file-audit.md`；
2. 输出 `appservices-file-audit.md`；
3. 依据两份 audit 开 Batch 1 真实 no-behavior 迁移 patch。

### 本轮验证

- 文档级验证：Batch 1 的目录、target、include root、compat、删除条件、build checkpoints 已落盘；
- 文档级验证：todo 与活跃切片已切到 file-level audit；
- 代码级验证：本轮仍未改 `Engine/Source` 行为代码。

## 2026-08-14 — 完成 Product/Host 与 AppServices file-level audit

### 本轮完成

- 新增 `product-host-file-audit.md`，把 `Product/Host/*` 逐文件划分为 app-form shell、consumer façade、branch-local adapter、compat only 与 dead/delete candidate；
- 新增 `appservices-file-audit.md`，确认 `Framework/AppServices/*` 不是共享 App owner，而是 render/runtime contract 与配置暂存桶；
- 关闭 Phase A2 file-level audit：`Foundation/Core/Application`、`Framework/AppRuntime`、`Framework/GUI/App`、`Framework/AppServices`、`Product/Host` 现在都有明确的 file-level owner 输入；
- 将活跃切片切换到 Phase A3 Batch 1 no-behavior migration。

### 当前结论

- Batch 1 现在已经具备真实执行前置：共享 `App/*` 与 `GUI/Host/*` 的迁移表齐全，`Product/Host` 与 `AppServices` 也明确了“只接新主线、不误判 owner”的边界；
- 下一步不再是继续写审计，而是按 `first-batch-move-design.md` 落目录、target、兼容头与消费者切换顺序；
- `Product/Host` 与 `Framework/AppServices` 暂不做物理挪动，避免把 Batch 1 的 no-behavior 迁移和 Batch 2 的 owner 拆桶混成一个 patch。

### 下一轮直接接力点

1. 建立 `App/Kernel`、`App/Control`、`GUI/Host` 的真实目录、target、public include root；
2. 增加最小 forward header / compat target 壳；
3. 先切 GUI-only consumers，再切 `ya-host` / `ya-editor`。

### 本轮验证

- 文档级验证：新增 `product-host-file-audit.md` 与 `appservices-file-audit.md`；
- 文档级验证：todo / feature matrix 已切到 Phase A3；
- 代码级验证：本轮仍未改 `Engine/Source` 行为代码。

## 2026-08-15 — 落地 Batch 1 no-behavior 迁移

### 本轮完成

- 建立三个真实 owner target：`ya-app-kernel`（App/Kernel）、`ya-app-control`（App/Control）、`ya-gui-host`（GUI/Host），各自带 public include root 与导出宏（`YA_APP_KERNEL_API` / `YA_APP_CONTROL_API` / `YA_GUI_API`）；
- `Foundation/Core/Application/*` 已 `git mv` 到 `App/Kernel` 与 `App/Control`；`AppKernel.h` 的宏由 `YA_CORE_API` 改为 `YA_APP_KERNEL_API`，4 个 control 头由 `YA_CORE_API` 改为 `YA_APP_CONTROL_API`；
- `Framework/AppRuntime/*` + `Framework/GUI/App/*` 已 `git mv` 到 `GUI/Host`；`AppBootstrap.h` / `NativeWindowManager.h` 的宏由 `YA_APP_RUNTIME_API` 改为 `YA_GUI_API`；
- 旧 public 头（`Core/Application/*`、`AppRuntime/*`、`GUI/App/*`）全部改写为指向新 public 头的 compat 转发头；`ya-app-runtime` / `ya-gui-app-host` 改写为 `set_kind("phony")` 的 re-export compat target，删除条件已写入注释（Phase A5 清理）；
- `Engine/Source/xmake.lua` 增删 includes、`ya_engine_defines()` 用 `YA_APP_KERNEL_API` + `YA_APP_CONTROL_API` 替换 `YA_APP_RUNTIME_API`；`ya-gui-framework` 与 `ya-engine` 增补 `ya-app-kernel` / `ya-app-control` 依赖；
- 切换全部消费者：GUI-only（`GUIWorkbench` / `ya-gui-minimal-host` / `ya-gui-headless-host-test` / `ya-gui-closure-test`）与 Product（`ya-host`）从旧 target 切到 `ya-gui-host`，所有旧 include 拼写迁移到 `App/Kernel`、`App/Control`、`GUI/Host`。

### 当前结论

- Batch 1 的目录/target/owner 物理收口已经落地：`App/Kernel (+ App/Control)` 成为无窗口主链，`GUI/Host` 成为唯一 GUI window/bootstrap/host owner；
- 旧 include 拼写在 `Engine` / `Example` / `Test` 中已零残留（`rg` 为空）；compat 转发头与 compat target 仅作为 Phase A5 的删除候选保留；
- `GUIWorkbench` 闭包仍只依赖 `ya-gui-host` + `ya-gui-tooling`，未被 `Product/Host` / `Game` 回灌。

### 下一轮直接接力点（Batch 2）

1. 依据 `product-host-file-audit.md` 拆出 `Product/Host` 内共享能力消费者；
2. 仅把剩余 app-form shell 归到具体 runtime/editor 分支；
3. 按 `appservices-file-audit.md` 拆回 `Framework/AppServices` 的真实 owner。

### 本轮验证

- 构建 checkpoint C1：`ya-app-kernel` / `ya-app-control` / `ya-gui-host` 单独构建通过；
- 构建 checkpoint C2：`ya-gui-closure-test` / `ya-gui-headless-host-test` / `ya-gui-minimal-host` / `GUIWorkbench` 构建通过；
- 构建 checkpoint C3：`ya-host` / `ya-editor` 构建通过；
- 构建 checkpoint C4：`rg 'Core/Application/|AppRuntime/|GUI/App/'` 在 Engine/Example/Test 零残留；
- 运行时：`ya-gui-closure-test` 114/114 PASS、`ya-gui-headless-host-test` 1/1 PASS、`ya-gui-minimal-host --exit-after-frame=30` EXIT=0。
