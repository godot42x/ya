# GUI 动画接入设计（独立切片）

> 建立日期：2026-08-15
> 定位：GUI 失效架构的**延后独立切片**，不混入 Phase 1A/2/3/4 正确性主线。
> 前置：Phase 1A（changed-only setter + property-aware binding）落地后，动画才有干净的接入面。
> 配套：`plan.md`（失效链不变式）、`todo.md`（登记为延后项）。

## 0. 核心判断

动画不是失效链的「例外」，而是失效链的一个**合法驱动源**。

当前架构的隐患正是「改了没标脏」。动画如果绕过失效链（每帧直接写 widget 字段），会重新引入这个 bug。因此动画接入的关键是给它一个**正确的失效入口**，而不是开后门。

动画的本质：一个**每帧必变、确定性驱动**的值。它天然映射到现有的 `Reactive<T>`。

## 1. 最小接入模型：动画值 = Reactive

```cpp
// 框架侧不需要新 tree 层、不需要新失效通道、不改 snapshot。复用 Reactive。
struct Tween
{
    Reactive<float> value;              // 当前值，widget paint 时读
    float from = 0.0f, to = 1.0f;
    float duration = 0.25f, elapsed = 0.0f;

    bool tick(float dt)                 // host 每帧调用（buildSnapshot 前）
    {
        elapsed += dt;
        const float t = glm::clamp(elapsed / duration, 0.0f, 1.0f);
        value.set(glm::mix(from, to, ease(t)));  // set() 触发标脏
        return t < 1.0f;                         // false = 动画结束
    }
};
```

widget 侧零改动：`paintSelf` 里读 `tween.value.get()` → 自动成为 dependent → tick 只重画该 widget。

覆盖 90% 需求，无需新概念。

## 2. 关键设计决策：值「paint 时解析」，不是「每帧 push」

- **推荐（Flutter 模式）**：动画对象是 `Reactive<T>`，widget 通过 binding 读它。动画 tick 只推进时间，不写 widget 字段。
- **反模式**：presenter 每帧 `widget->_color = 当前插值`（正是当前 `_bVolatile` 兜底在做的；GI-004 基线里 `notify=0`、交互帧 rebuilt 76% 的根因）。

推荐形态的好处：动画结束、值不再变时，widget 自动回到「干净」状态，不再每帧重画。这与 Phase 1A 的 setter/binding 语义是同一延伸。

## 3. 三个层次，按需渐进

| 层次 | 内容 | 何时用 |
|---|---|---|
| 层次一 | 动画值 = `Reactive<T>` + host tick | 先做，覆盖绝大多数 |
| 层次二 | 动画驱动 dirty（绕开 notify 开销） | 游戏内高频动画；`_bVolatile` 的精确化 |
| 层次三 | 动画控制器/编排（循环/事件/序列/缓动） | 应用层，editor 和游戏各自按需 |

### 层次二的语义

动画是确定性每帧必变的，`set()` 走 notify 依赖遍历是浪费。改为：

- 动画 widget 持「动画进行中」标记；
- 动画 tick 直接 `markPaintDirty` 依赖 widget，paint 时读动画当前值；
- **即 `_bVolatile` 的精确化**：`_bVolatile` 是「永远每帧重画」，动画标记是「动画期间每帧重画，结束恢复」。

只有 profile 证明 notify 遍历是热点时才做，默认留在层次一。

## 4. editor vs 游戏内

框架层**共享**（两者最终都是 `WidgetTree` + host tick），差异只在业务侧：

| | editor 自身 | 游戏内 |
|---|---|---|
| 动画种类 | 面板展开/折叠、tab 切换、选中高亮过渡、Inspector 数值 | 血条平滑、伤害数字、技能冷却、按钮弹跳、页面转场 |
| 频率/开销敏感度 | 低（ImGui 部分 immediate，本身每帧重建） | 高（帧预算紧） |
| 推荐层次 | 层次一 | 层次一 + 层次二 + 层次三 |

**editor 特殊点**：ImGui 部分是 immediate mode，动画天然每帧算、不走 WidgetTree；只有 retain UI 部分走这套。所以「editor 的动画」要先分清是 ImGui 面板动画（无需框架支持）还是 retain UI 动画（走 WidgetTree）。

## 5. 与 Phase 计划的关系

- **Phase 1A（setter/binding）是前置**：动画需通过 changed-only setter 或 binding 写值，而不是 presenter 每帧直接写字段。Phase 1A 设计时预留「动画值可绑定」语义。
- **Phase 3（batching）**：动画每帧 notify 大量依赖时 batching 能合并，但动画通常依赖少，**不构成启动 batching 的理由**。
- **Phase 4（subtree boundary）**：对游戏 UI 重要（高频动画区 vs 静态区隔离），但仍按 profile 数据决定。

## 6. 决策记录

- 动画值建模为 `Reactive<T>`，tick 挂在 host 层（`updateUI`，`buildSnapshot` 前）。
- 不新增 tree 层、失效通道、不改 snapshot。
- 值 paint 时解析，不每帧 push 字段。
- 动画不单独启动 batching 或 boundary。
- editor/游戏共享框架层，业务侧按需加动画控制器。

## 7. 启动条件

本切片不阻塞主线。启动条件：

1. 出现第一个真实动画需求（editor 或游戏内的 retain UI）；
2. Phase 1A 已落地（setter/binding 可用）；
3. 若做层次二，需 GI-004 基线证明 notify/依赖遍历是热点。
