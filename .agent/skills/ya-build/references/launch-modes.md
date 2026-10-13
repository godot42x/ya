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

# make 等价（薄包装，t/ARGS 可覆盖）
make b t=GUIWorkbench
make r t=GUIWorkbench ARGS="--smoke-actions"
make test
```

### 测试源码布局（新增用例时看这里）

`Engine/Test/Source/` 按**门禁（gate）**分目录，每个 target 用目录 glob 收源码
（`ya_test_sources("Support", "<Gate>")`），所以新用例只要放进对的目录就自动进闸门，
不需要改 xmake：

| 目录 | 谁编译它 | 说明 |
| --- | --- | --- |
| `Support/` | 全部测试 target | 共享 runner (`TestEntry.cpp`) 与测试辅助头（如 `TestSource.h`） |
| `Integration/` | 仅 `ya-testing` | 需要整条引擎链路（App/Scene/Editor/Runtime）的用例 |
| `EcCore` `ResourceCore` `Render2D` `Render3D` `ResourceRuntime` `RhiVulkan` | 对应 closure target | 只链接该模块的闭包回归 |
| `GuiDeclarative` | `ya-gui-declarative-contract-test`、`ya-gui-widgets-test`、`ya-gui-closure-test` | declarative 契约 |
| `GuiWidgets` | `ya-gui-widgets-test`、`ya-gui-closure-test` | 只依赖 widgets 的契约 |
| `GuiFramework` | `ya-gui-closure-test` | 需要整个 GUI closure |
| `GuiHost` | `ya-gui-headless-host-test` | host / 窗口 / chrome |

两条硬约定：

1. **不要用子节点下标定位控件**：布局会变，下标会静默变成 `nullptr` 让断言失效。按
   authored key（`_stableKey`，直构控件在 `_name`）或类型查找，见
   `EditorPropertyGraphTest` 的 `findControlByKeySuffix`。
2. **读源码文本的用例**用 `#include "TestSource.h"` 的 `readEngineSource()`：它按仓库根
   标记（`Engine/Source` + `Xmake`）向上查找，不要写 `__FILE__` 加固定 `../..` 层数——
   用例换一个 suite 目录就会静默读到空文件。

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



## 实例与生命周期（多 agent / 长驻运行）

需要驱动引擎（自动化、脚本、MCP、截图、回归）时，**走 `control` 入口，不要手搓启动命令**：

```bash
python3 Script/ya.py control start --project Example/HelloMaterial/HelloMaterial.yaproject
python3 Script/ya.py control status
python3 Script/ya.py control call --project <p> eval_js '{"source": "1+1"}'
python3 Script/ya.py control mcp --project <p>        # stdio MCP，工具目录 = 引擎 list_commands
python3 Script/ya.py control stop --project <p>       # 退出；不听话则 SIGTERM→SIGKILL
```

`control start` 的行为就是「有则接入、无则只起一个」：

- 同 project 同模式已有实例 → **直接复用**（打印它的 pid 与端口），不再开第二个进程；
- 需要新起时，自己挑空闲端口、脱离当前 shell 存活，并**默认带 30 分钟墙钟上限**
  （`--lifetime 0` 才是显式取消）；
- 实例把自己登记在 `tempfile.gettempdir()/ya-instances/*.instance.json`（pid/project/mode/
  controlPort/启动时间），所以「谁在跑、端口是多少」是查询而不是猜；
- 进程被 SIGKILL 后记录可能残留，`status`/`stop` 会按 pid 存活情况清掉，不必手工删文件。

引擎侧的同一条规则（`ya-runtime`，game 与 editor 同一套入口）：

- 同一 project 同一模式只允许一个实例；第二个实例立刻以退出码 1 停止，日志给出
  `Refusing to start '<project>|editor': already running as pid <PID> (automation control port <p>)`
  —— 消息里直接给出可接入的端口，接到活的那一个而不是再起一个。不同 project 可以并存
  （合法的双开），锁由内核在进程退出或被 kill 时释放，不需要手工删锁文件。
- 长驻运行（agent 调试、录制、自动化）统一带上墙钟上限 `--max-lifetime-seconds=<s>`：
  到点退出码 0，日志 `App loop exited: reason=max-lifetime after N frames, Ts`。
  `--exit-after-frame` 只数帧，挡不住卡住或闲置的进程，墙钟才会。
- 请求了 `--automation-control-port=<p>` 而端口已被占用时，启动直接失败（退出码 1，
  `automation control port <p> is already in use`），不会出现「进程看起来健康、但自动化
  调用实际打在别人身上」。
- 脚本里 Popen 起引擎的：`engine.kill()` 杀的是启动器（`ya.py`/`xmake`），**不是引擎**。
  这类 harness 必须给引擎传 `--max-lifetime-seconds`，否则 harness 自己死掉就留下孤儿。
- 收工前确认没有自己的残留实例：

```bash
python3 Script/ya.py control status
ps -ax -o pid,etime,command | grep ya-runtime | grep -v grep
```


