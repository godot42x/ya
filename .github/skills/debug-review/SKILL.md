---
name: ya-debug-review
description: YA Engine 常见崩溃排查与变更复盘
---

## 适用场景

- 运行时崩溃、访问越界、空指针问题
- 提交前自检、代码 review、diff 复盘

## 崩溃排查优先级

1. `0xC0000005`：先查空指针/悬空指针
2. 初始化顺序：确认 `IRender` 等核心对象先初始化后使用
3. 资源生命周期：检查 `shared_ptr` 持有链与释放时机

## 日志与观测

优先使用：
- `YA_CORE_TRACE`
- `YA_CORE_DEBUG`
- `YA_CORE_INFO`
- `YA_CORE_WARN`
- `YA_CORE_ERROR`
- `YA_CORE_ASSERT`

## Review 检查单

1. `git status` / `git diff` 仅包含必要改动
2. 删除重复或无效逻辑
3. 可复用逻辑适度抽离，保持模块边界清晰
4. 临时文件与一次性文件及时清理

## 退出条件

- 关键崩溃路径已定位或修复，且变更可解释、可回归
