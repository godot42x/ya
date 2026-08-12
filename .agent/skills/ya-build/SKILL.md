---
name: ya-build
description: YA Engine 的 XMake 构建、项目运行、shader 生成与测试运行指南。
---

## 适用场景

- 构建失败、链接失败、目标找不到、编译参数不对
- 需要选择正确的入口进行 build / run / test
- 需要刷新 `compile_commands.json` 或重跑 shader 生成
- 需要判断应该用 `python3 Script/ya.py` 工作流命令还是直接用 `xmake`

## 核心规则

1. 只使用 XMake；不要引入 CMake。
2. 默认宿主目标固定为 `ya-runtime`；项目构建/运行只通过 `--project ...` 驱动，编辑器走 `run-editor`。
3. shader 生成通过 `xmake ya-shader`，不要手改 `Engine/Shader/*/Generated/*`。
4. 测试使用 GoogleTest 目标，不要自己发明另一套测试入口。
5. 不要再为 project build/run/package 使用 legacy `--target` 语义。
6. `python3 Script/ya.py package` 的定位是最小收集器，不默认承诺完整跨平台可分发包。

## 常用命令

### Python launcher（优先）

```bash
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py build --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py package --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py test --target ya --filter Suite.Test
python3 Script/ya.py cfg
```

### 直接使用 XMake

```bash
xmake
xmake b TargetName
xmake run TargetName
xmake l targets
xmake f -m debug -y
xmake ya-shader
xmake b ya-testing && xmake r ya-testing --gtest_filter=Suite.Test
```

## 两种启动模式

仓库只有两类入口，按"是否经过引擎项目流程"区分，不要混用：

### 模式 1：直接运行某个 target（xmake / make b|r）

适用于 standalone 可执行 target（GUI example、测试、引擎模块），不经过
`ya.py` 的项目流程：

```bash
# GUI example（只链接 GUI closure，不依赖 Scene/ECS/Host/Editor）
xmake b GUIWorkbench && xmake r GUIWorkbench                 # retain-mode 工具 app
xmake r GUIWorkbench --smoke-actions                        # 端到端自动化（退出码=PASS/FAIL）
xmake b ya-gui-minimal-host && xmake r ya-gui-minimal-host --exit-after-frame=30

# GUI 测试 target
xmake b ya-gui-widgets-test && xmake r ya-gui-widgets-test
xmake b ya-gui-closure-test && xmake r ya-gui-closure-test
xmake b ya-gui-workbench-workspace-test && xmake r ya-gui-workbench-workspace-test

# make 等价（薄包装，TARGET/ARGS 可覆盖）
make b TARGET=GUIWorkbench
make r TARGET=GUIWorkbench ARGS="--smoke-actions"
```

### 模式 2：项目相关 / editor（Script/ya.py）

适用于 game 项目（`ya-runtime` + `.yaproject`）与 editor：

```bash
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py run-editor --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py build --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py package --project Example/HelloMaterial/HelloMaterial.yaproject

# 自动化冒烟：跑 N 帧后自动退出
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject \
    -- --exit-after-frame=120 --log-level=warn

# make 等价
make run t=HelloMaterial
make run-editor t=HelloMaterial
make build t=HelloMaterial
make package t=HelloMaterial
```

要点：

- `ya.py` 的子命令只负责引擎项目流程（`cfg / build / run / run-editor / package /
  test`）；GUI/模块/测试 target 一律直接 `xmake b|r`；
- `GUIWorkbench --smoke-actions` 是端到端自动化冒烟（走真实事件路径驱动
  workbench 交互），PASS/FAIL 决定退出码；
- GUIAppHost 默认开启 vsync（FIFO）；关闭 vsync 会退化为 Immediate present，
  直出 swapchain 会出现撕裂/闪烁；
- GUI example 属于 gui profile 也能构建：`xmake f --ya_profile=gui` 后构建同一批目标。

## Profiling

- profiling、automation trace 与 speedscope 规则已独立到 `profiling` skill。
- 若任务重点是 profile 模式、CPU trace、RenderDoc、automation 配置，转到 `./profiling/SKILL.md`。
- 最小入口仍是：

```bash
python3 Script/ya.py cfg --mode profile
python3 Script/ya.py run --project Example/HelloMaterial/HelloMaterial.yaproject -- --exit-after-frame=300 --log-level=warn --log-detail-level=error
```

## 何时用哪种入口

1. 日常构建 / 运行 / 刷新 compile commands，优先用 `python3 Script/ya.py`。
2. 需要精确控制某个 xmake 子命令时，直接用 `xmake`。
3. 需要刷新工作区构建配置时，用 `python3 Script/ya.py cfg`。
4. 需要重建 shader 头时，用 `xmake ya-shader`。
5. 需要收集最小项目包时，用 `python3 Script/ya.py package --project <path>`。
6. 若问题已经进入平台发行、app bundle、签名、图形驱动分发，先明确那不是当前默认 package 目标，再决定是否单独扩工作范围。

## 当前仓库锚点

- `Script/ya.py`：引擎项目流程入口，封装 `cfg / build / run / run-editor / package / test`
- `makefile`：两种模式的薄包装（模式 1：`make b|r TARGET=...`；模式 2：
  `make run|run-editor|build|package t=...`）
- `xmake.lua`：全局规则、`compile_commands.autoupdate`
- `Engine/Shader/Shader.xmake.lua`：shader 生成入口
- `Test/xmake.lua`：测试目标定义
- `Example/`：可运行示例目标

## 常见排查顺序

### 1. 目标找不到

1. 先运行 `xmake l targets` 确认 xmake 目标名。
2. 再确认当前要跑的是 `ya-runtime + project=...`，还是 `*-testing`。
3. 若是 VS Code 启动项问题，再联动看 `vscode` skill。

### 2. 编译通过但运行入口不对

1. 优先检查是否应该使用 `python3 Script/ya.py run --project <path>`。
2. 确认 `launch.json` 中程序路径是否仍匹配当前输出目录。

### 3. shader 相关报错

1. 先确认是不是生成头过期。
2. 运行 `xmake ya-shader`。
3. 若仍失败，回看 `Engine/Shader/Shader.xmake.lua`、`slang_gen_header.py`、`glsl_gen_header.py`。
4. 不要直接编辑 `Generated/*.slang.h` 或 `Generated/*.glsl.h`。

### 4. IntelliSense / clangd 不对

1. 先刷新 `compile_commands.json`：`python3 Script/ya.py cfg`。
2. 再检查 VS Code 是否读取了正确文件。
3. 这类问题通常和 `vscode` skill 一起看。

### 5. 测试运行失败

1. 确认目标是 `$(t)-testing` 或 `ya-testing`。
2. 确认 `--gtest_filter` / `r_args` 写法正确。
3. 若只是单测不过，构建链路没问题时再转去具体模块 skill。

## 共享缓存（多 worktree / 多 agent 并行）

大体积产物（Vulkan SDK、重型 submodule）以**真实文件**存放在主项目自己的
`Engine/ThirdParty` 里（SDK 在 `Engine/ThirdParty/VulkanSDK/`），其他
worktree 通过目录链接（macOS/Linux symlink，Windows junction）指回主项目
的同一份，避免各自下载/存储。系统目录（~/Library/Caches 等）从不写入：
删除主项目目录即整体清理，不留垃圾。

- 主项目解析：`$YA_CACHE_ROOT` → `git config ya.cacheRoot`（独立 clone 想
  共享时设成主项目路径）→ 自动推导（git worktree 的 `--git-common-dir`
  指向主项目，所有 worktree 自动共享，零配置）。
- 主项目本身保持标准 git 语义：submodule 是真实 checkout，
  `git submodule status/update` 正常可用；只有链接的 worktree 需要
  skip-worktree。
- 旧版布局（系统缓存 `~/Library/Caches/ya-engine`、`<主>/.ya-cache`、以及
  非主 checkout 里的真实目录）会在首次运行时自动迁移回主项目原位，不会
  重新下载。
- macOS Vulkan SDK：`Script/setup_vulkan_sdk_macos.py` 装到
  主项目的 `Engine/ThirdParty/VulkanSDK/`（`YA_VULKAN_SDK_ROOT` 可单独
  覆盖），主项目内是真实目录，worktree 内是指向它的 symlink；安装原子化
  （pid 唯一临时目录 + rename），并发执行不会损坏。
- 重型 submodule（`Vulkan-Samples-Assets` ~1GB、`LearnOpenGL` ~250MB）：
  `Script/setup_submodules.py` 在主项目里保持真实 checkout（标准 git），
  worktree 内路径是指回主项目的链接，通过 `git update-index --skip-worktree`
  保持 `git status` 干净。worktree 看到的 submodule 内容跟随主项目。

注意：
- 在**链接的 worktree** 里，重型 submodule 路径上不要跑裸
  `git submodule status/update`（会报 "expected submodule path ... not to be
  a symbolic link"）；主项目里完全正常。小 submodule（ImGui 等）到处都保持
  标准 git 语义。
- 新 clone 先跑 `ya.py cfg` 再手动 `git submodule update`，否则重型
  submodule 会先被完整拉下来；脚本会把干净的 checkout 自动转回链接。
- 不要在主项目里跑 `git clean -fdx`（会清掉 gitignore 的
  `Engine/ThirdParty/VulkanSDK/` 触发重新下载）；误清后重跑
  `python3 Script/ya.py vulkan-sdk-macos` 即可恢复。
- 若链接目标异常，先跑 `python3 Script/ya.py cfg` 再看构建。

## 相关 skills

- `vscode`：处理任务、launch 配置、compile_commands 与调试链路
- `profiling`：profile 模式、automation trace、低噪音性能冒烟
- `render-arch`：改到 shader、RenderRuntime、后端边界时一起看
- `resource-system`：资源链路改动引起的编译或生成问题时一起看
- `material-flow`：材质 shader / UBO 变更引起的构建问题时一起看
- `cpp-style`：修编译错误同时保持风格与最小改动时一起看
- `debug-review`：构建修复后做提交前自检时一起看

## 不建议的方向

1. 不要在仓库里引入 CMake 文件或 CMake 专用说明。
2. 不要直接修改生成文件来“修构建”。
3. 不要把 VS Code 专属问题误判成构建系统问题。
4. 不要在一次修构建时顺手做无关重构。

## 退出条件

- 用户知道该用哪个命令入口
- 构建 / 运行 / 测试 / shader 生成路径至少有一条明确可用
- 问题已收敛为构建配置、生成链、目标选择，或已转交给更合适的 skill
