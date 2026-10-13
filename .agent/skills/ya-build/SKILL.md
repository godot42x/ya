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

### 跑全部测试（group）

每个测试 target 都带 `set_group("test")`，所以全量测试不需要手维护 target 列表：

```bash
make test        # 等价于 xmake test
xmake b -g test  # 只构建全部测试 target
xmake r -g test  # 只运行（构建已就绪时）
```

`xmake test` 这个 action 定义在 `Engine/Test/Test.xmake.lua`（两个 profile 都 include）。
group 内 target 顺序由 xmake 决定，**遇到第一个红 target 就停下**：它是“修完再跑”
的闸门，不是“汇总报告”。要单独定位时直接 `xmake r <target>`，不要用二进制路径直接跑
（`set_rundir` 把工作目录钉在仓库根，直接跑会让相对资源路径失效，产生假失败）。


## 何时用哪种入口

1. 日常构建 / 运行 / 刷新 compile commands，优先用 `python3 Script/ya.py`。
2. 需要精确控制某个 xmake 子命令时，直接用 `xmake`。
3. 需要刷新工作区构建配置时，用 `python3 Script/ya.py cfg`。
4. 需要重建 shader 头时，用 `xmake ya-shader`。
5. 需要收集最小项目包时，用 `python3 Script/ya.py package --project <path>`。
6. 若问题已经进入平台发行、app bundle、签名、图形驱动分发，先明确那不是当前默认 package 目标，再决定是否单独扩工作范围。


## 细节参考（按需读，不要全载）

| 何时读 | 文件 |
| --- | --- |
| 启动 target / 项目 / editor、worktree、实例与生命周期 | `references/launch-modes.md` |
| profiling、共享缓存、多 agent 并行构建锁 | `references/profiling-and-locks.md` |
| 构建或测试报错的排查顺序 | `references/troubleshooting.md` |

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

