# 重排 unity 批次会暴露平铺时被掩盖的重复私有符号

> 2026-09-18，`GameEditor/UI` 关切分组（`editor-ui-grouping` G1）时发生。

## 现象

纯文件搬迁（91 个 rename，零行改动）之后 `xmake b ya-game-editor` 直接报
**重复定义**：`FEmptyGuiDelegate`、`dockHasPanels` 在同一个 unity TU 里出现两次。
平铺布局时这两个 .cpp 落在不同批次，问题不存在；分组改变了
`add_files("**.cpp")` 的顺序，两个文件挤进同一个 `unity_7`，冲突才浮出来。

## 根因

xmake 的 unity build 把同一 target 的多个 `.cpp` 拼进一个 TU。
`.cpp` 里的文件级私有符号（匿名 namespace 之外的结构体、自由函数）
**只有在不撞批次时才是私有的**。平铺目录让"谁和谁同批次"无从预测，
于是这类重复定义长期处于休眠状态。

搬家不是引入 bug，是把休眠 bug 唤醒。

## 预防

1. **任何批量搬迁后必须重新构建该 target**，不能只看 rename 数就收工。
2. 不要用"加命名空间"糊过去：同文件里其他自由函数（如
   `hostDockOnTree`）仍按全仓库风格非限定调用，单独给两个符号加限定
   会让该文件风格分裂。正确做法是**抽私有头**（同目录、裸文件名 include），
   或明确改成不同名字。
3. 搬迁时顺手做"同义不同名"收敛：本轮 `editorWindowHasPanels` 与
   `dockHasPanels`/`sessionHasDockPanels` 语义重复，合并进私有头。
4. 判断是否有隐藏冲突：搬迁前后各跑一次完整 target 构建；如果搬迁后出现
   只在某个 unity 批次里的 duplicate symbol，基本就是本条。

## 相关

- `./module_split_sed_regression.md`：同类"编译/测试全绿但结构已坏"的另一种形态。
- `./legacy_test_target_break_after_module_move.md`：模块迁移后遗留 target 的取用失败清单。
